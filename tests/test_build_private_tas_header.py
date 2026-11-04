#!/usr/bin/env python3
"""Ensure private TAS data is hash-checked and never emitted into the repo."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools/build-private-tas-header.py"


class HeaderTest(unittest.TestCase):
    def test_hash_and_output_location(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            data = b"MI8TAS1\0" + bytes(8)
            source, output = base / "data", base / "header.h"
            source.write_bytes(data)
            digest = hashlib.sha256(data).hexdigest()
            cmd = [sys.executable, str(TOOL), str(source), "--sha256", digest, "--output"]
            self.assertEqual(subprocess.run(cmd + [str(output)], capture_output=True).returncode, 0)
            self.assertNotEqual(subprocess.run(cmd + [str(output)], capture_output=True).returncode, 0)
            self.assertNotEqual(subprocess.run([sys.executable, str(TOOL), str(source), "--sha256", "0" * 64,
                                                "--output", str(base / "bad")],
                                               capture_output=True).returncode, 0)
            self.assertNotEqual(subprocess.run(cmd + [str(ROOT / "private-header-test.h")],
                                               capture_output=True).returncode, 0)
            self.assertFalse((ROOT / "private-header-test.h").exists())


if __name__ == "__main__":
    unittest.main()
