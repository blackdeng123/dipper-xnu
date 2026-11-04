#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build a private, unpowered TAS2557 init transcript; never accesses a phone.

The input comes from the user's own validated Simpleperf and ftrace captures.
The output contains vendor firmware and per-device calibration bytes: it must
be a new file outside this repository and is not a distribution artifact.
The kernel must still validate it, verify its provenance, and hold reset on
any failure. This transcript never supplies the missing playback/power path.
"""
import argparse
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[2]
CAL = re.compile(r"\s(\d+)\.(\d+): i2c_write: i2c-(\d+) #(\d+) a=([0-9a-f]+) f=([0-9a-f]+) l=(\d+) \[([^\]]+)\]")


def build(init, cal_text):
    if init.get("format") != 1 or init.get("bus") != 2 or init.get("address") != 0x4c:
        raise ValueError("unexpected initialization capture identity")
    events = init.get("events", [])
    if init.get("sample_count") != len(events) or not events:
        raise ValueError("initialization capture is incomplete")
    records = []
    i = 0
    last = events[0]["time_ns"]
    while i < len(events):
        event = events[i]
        if event["event"] != "write" or event["message"] != 0:
            raise ValueError("expected the first I2C message of a transaction")
        written = bytes.fromhex(event["data_hex"])
        when = event["time_ns"]
        if when < last:
            raise ValueError("timestamps are out of order")
        delay = (when - last + 999) // 1000
        reply = b""
        last = when
        if i + 1 < len(events) and events[i + 1]["event"] == "reply":
            response = events[i + 1]
            if len(written) != 1 or response["message"] != 1 or response["time_ns"] < when:
                raise ValueError("read response is not paired with a register pointer")
            reply = bytes.fromhex(response["data_hex"])
            last = response["time_ns"]
            i += 1
        elif len(written) < 2:
            raise ValueError("unpaired register pointer")
        records.append((delay, written, reply))
        i += 1
    header = re.search(r"entries-in-buffer/entries-written:\s*(\d+)/(\d+)", cal_text)
    if not header or header.group(1) != header.group(2):
        raise ValueError("calibration trace has missing events")
    cal = list(CAL.finditer(cal_text))
    if len(cal) != int(header.group(1)) or not cal:
        raise ValueError("calibration trace has missing or unexpected events")
    last = None
    for match in cal:
        sec, frac, bus, number, address, flags, size, data = match.groups()
        if (int(bus), int(number), int(address, 16), int(flags, 16)) != (2, 0, 0x4c, 0):
            raise ValueError("unexpected calibration transaction")
        payload = bytes.fromhex(data.replace("-", " "))
        if len(payload) != int(size):
            raise ValueError("truncated calibration transaction")
        when = int(sec) * 1000000000 + int(frac.ljust(9, "0")[:9])
        if last is not None and when < last:
            raise ValueError("calibration timestamps are out of order")
        records.append((1000 if last is None else (when - last + 999) // 1000, payload, b""))
        last = when
    if len(records) > 1024:
        raise ValueError("too many transactions")
    output = bytearray(b"MI8TAS1\0" + struct.pack("<I", len(records)))
    for delay, written, reply in records:
        if delay > 100000 or not written or len(written) > 128 or len(reply) > 120:
            raise ValueError("transaction exceeds interpreter bounds")
        output += struct.pack("<IHH", delay, len(written), len(reply)) + written + reply
    return output, len(records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("init_json", type=Path)
    parser.add_argument("cal_trace", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.is_relative_to(ROOT):
        parser.error("device-private data must be stored outside the repository")
    data, count = build(json.loads(args.init_json.read_text(encoding="utf-8")),
                        args.cal_trace.read_text(encoding="utf-8"))
    with output.open("xb") as f:
        f.write(data)
    print(f"Prepared {count} private transactions; kernel validation and hardware verification remain required.")


if __name__ == "__main__":
    main()
