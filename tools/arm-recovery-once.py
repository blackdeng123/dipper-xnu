#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Arm one recovery boot from a connected dipper TWRP, preserving misc metadata.

Android bootloader message layout: https://android.googlesource.com/platform/bootable/recovery/+/refs/heads/main/bootloader_message/include/bootloader_message/bootloader_message.h
No AOSP code is copied. This explicitly changes the bootloader command field.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile
import time

MISC = "/dev/block/bootdevice/by-name/misc"
BLOCK = 4096
MESSAGE = 2048
PARTITION_SIZE = 4 * 1024 * 1024


def arm_block(original):
    if len(original) != BLOCK:
        raise ValueError("Unexpected misc block length")
    if any(original[:MESSAGE]):
        raise ValueError("Existing bootloader message is not empty; refusing to overwrite it")
    result = bytearray(original)
    result[:32] = b"boot-recovery\0".ljust(32, b"\0")
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--backup-dir", required=True, type=Path)
    parser.add_argument("--adb-port", default="5038")
    args = parser.parse_args()
    adb = ["adb", "-P", args.adb_port, "-s", args.serial]

    def run(*command, **kwargs):
        return subprocess.run([*adb, *command], check=True, **kwargs)

    product = run("shell", "getprop", "ro.product.device", capture_output=True).stdout.strip()
    state = run("get-state", capture_output=True).stdout.strip()
    if product != b"dipper" or state != b"recovery":
        raise ValueError("Expected dipper in recovery mode")
    size = int(run("shell", "blockdev", "--getsize64", MISC, capture_output=True).stdout.strip())
    if size != PARTITION_SIZE:
        raise ValueError("Unexpected misc partition size")
    original = run("exec-out", f"dd if={MISC} bs=4096 count=1024 2>/dev/null", capture_output=True).stdout
    if len(original) != size:
        raise ValueError("Could not read the entire misc partition")
    updated = arm_block(original[:BLOCK])
    args.backup_dir.mkdir(parents=True, exist_ok=True)
    backup = args.backup_dir / ("misc-before-recovery-" + time.strftime("%Y%m%d-%H%M%S") + ".img")
    with backup.open("xb") as stream:
        stream.write(original)
        stream.flush()
        import os
        os.fsync(stream.fileno())
    print("misc backup:", backup, "SHA-256:", hashlib.sha256(original).hexdigest(), flush=True)
    if run("exec-out", f"dd if={MISC} bs=4096 count=1 2>/dev/null", capture_output=True).stdout != original[:BLOCK]:
        raise ValueError("misc changed during backup; refusing to write")
    with tempfile.TemporaryDirectory() as directory:
        local = Path(directory) / "misc-recovery-first-block.bin"
        local.write_bytes(updated)
        remote = "/tmp/codex-misc-recovery-first-block.bin"
        run("push", str(local), remote)
        run("shell", "dd", f"if={remote}", f"of={MISC}", "bs=4096", "count=1")
        run("shell", "sync")
    readback = run("exec-out", f"dd if={MISC} bs=4096 count=1 2>/dev/null", capture_output=True).stdout
    if readback != updated:
        raise RuntimeError("misc readback differs; do not reboot until inspected")
    print("One-shot recovery command written and first block read back intact.")


if __name__ == "__main__":
    main()
