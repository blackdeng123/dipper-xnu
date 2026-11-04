#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Extract private I2C tracepoint bytes from an Android simpleperf dump.

The input is text produced by the matching Android simpleperf `dump` command,
not its formatted ftrace output. The output can contain device calibration or
vendor firmware and is deliberately restricted to a new file outside this
repository. This utility never sends I2C commands or operates on a device.
"""
import argparse
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
WORD = re.compile(r"^\s+0x([0-9a-fA-F]{8}) \(\d+\)\s*$", re.MULTILINE)


def extract(text, write_id, reply_id, bus, address):
    events = []
    samples = 0
    for block in text.split("record sample:")[1:]:
        size_match = re.search(r"^\s+raw size=(\d+)\s*$", block, re.MULTILINE)
        time_match = re.search(r"^\s+time (\d+)\s*$", block, re.MULTILINE)
        if not size_match or not time_match:
            raise ValueError("sample is missing a timestamp or raw payload")
        size = int(size_match.group(1))
        words = WORD.findall(block)
        raw = b"".join(struct.pack("<I", int(word, 16)) for word in words)[:size]
        if len(raw) != size or size < 24:
            raise ValueError("truncated tracepoint sample")
        samples += 1
        event_id = struct.unpack_from("<H", raw)[0]
        if event_id not in (write_id, reply_id):
            continue
        adapter = struct.unpack_from("<i", raw, 8)[0]
        number, target, flags, length = struct.unpack_from("<HHHH", raw, 12)
        location = struct.unpack_from("<I", raw, 20)[0]
        offset, payload_size = location & 0xffff, location >> 16
        if offset < 24 or payload_size != length or offset + payload_size > len(raw):
            raise ValueError("invalid I2C tracepoint payload bounds")
        if adapter != bus or target != address:
            continue
        expected_flags = 0 if event_id == write_id else 1
        if flags != expected_flags:
            raise ValueError("unsupported I2C message flags")
        events.append({"time_ns": int(time_match.group(1)),
                       "event": "write" if event_id == write_id else "reply",
                       "message": number, "data_hex": raw[offset:offset + payload_size].hex()})
    if not events:
        raise ValueError("no matching device events")
    if any(a["time_ns"] > b["time_ns"] for a, b in zip(events, events[1:])):
        raise ValueError("events are out of timestamp order")
    return samples, events


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("dump", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--write-event-id", type=int, required=True)
    p.add_argument("--reply-event-id", type=int, required=True)
    p.add_argument("--expected-samples", type=int, required=True,
                   help="count reported by simpleperf; independently confirm zero lost samples")
    p.add_argument("--bus", type=int, default=2)
    p.add_argument("--address", type=lambda s: int(s, 0), default=0x4c)
    args = p.parse_args()
    output = args.output.resolve()
    if output.is_relative_to(ROOT):
        p.error("private device data must be written outside the repository")
    if args.write_event_id == args.reply_event_id or args.expected_samples <= 0:
        p.error("event IDs must differ and sample count must be positive")
    samples, events = extract(args.dump.read_text(encoding="utf-8"), args.write_event_id,
                              args.reply_event_id, args.bus, args.address)
    if samples != args.expected_samples:
        raise ValueError(f"expected {args.expected_samples} samples, found {samples}")
    with output.open("x", encoding="utf-8") as f:
        json.dump({"format": 1, "bus": args.bus, "address": args.address,
                   "sample_count": samples, "events": events}, f, separators=(",", ":"))
        f.write("\n")
    print(f"Extracted {len(events)} device events from {samples} samples to a private file.")


if __name__ == "__main__":
    main()
