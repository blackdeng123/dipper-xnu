#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Embed a locally verified private TAS transcript into an isolated kernel.

The generated header contains device firmware and calibration data. It must be
created outside this repository and must not be distributed. This tool does
not compile a kernel or operate on a phone.
"""
import argparse
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("transcript", type=Path)
    p.add_argument("--sha256", required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    output = args.output.resolve()
    if output.is_relative_to(ROOT):
        p.error("the private header must be outside the repository")
    if not re.fullmatch(r"[0-9a-fA-F]{64}", args.sha256):
        p.error("--sha256 must contain exactly 64 hexadecimal digits")
    data = args.transcript.read_bytes()
    if not (12 <= len(data) <= 100000) or data[:8] != b"MI8TAS1\0":
        p.error("invalid transcript header or size")
    if hashlib.sha256(data).hexdigest() != args.sha256.lower():
        p.error("transcript SHA-256 differs from the expected value")
    with output.open("x", encoding="ascii", newline="\n") as f:
        f.write("/* Device-private file. Do not distribute. */\n#define MI8_TAS_PRIVATE_AVAILABLE 1\n")
        f.write("static const unsigned char mi8_tas_private_data[] = {\n")
        for start in range(0, len(data), 16):
            f.write("    " + ",".join(f"0x{x:02x}" for x in data[start:start + 16]) + ",\n")
        f.write("};\n")
    print(f"Private header generated outside repository; {len(data)} bytes.")


if __name__ == "__main__":
    main()
