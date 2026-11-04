#!/usr/bin/env python3
"""Check complete and malformed Android simpleperf tracepoint records."""
import importlib.util
from pathlib import Path
import struct
import unittest

path = Path(__file__).resolve().parents[1] / "tools/extract-i2c-perf.py"
spec = importlib.util.spec_from_file_location("extract_i2c_perf", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def sample(event_id=771, bus=2, address=0x4c, flags=0, payload=b"\x01\x02", truncate=False):
    raw = bytearray(24 + len(payload))
    struct.pack_into("<H", raw, 0, event_id)
    struct.pack_into("<iHHHHI", raw, 8, bus, 0, address, flags, len(payload), 0)
    struct.pack_into("<I", raw, 20, len(payload) << 16 | 24)
    raw[24:] = payload
    raw.extend(bytes((-len(raw)) % 4))
    words = [struct.unpack_from("<I", raw, i)[0] for i in range(0, len(raw), 4)]
    if truncate:
        words.pop()
    return ("record sample: type 9, misc 1, size 100\n  time 100\n  raw size=" + str(len(raw)) + "\n" +
            "".join(f"    0x{word:08x} ({word})\n" for word in words))


class ExtractTest(unittest.TestCase):
    def test_full_long_message(self):
        data = bytes(range(121))
        count, events = module.extract(sample(payload=data), 771, 769, 2, 0x4c)
        self.assertEqual(count, 1)
        self.assertEqual(bytes.fromhex(events[0]["data_hex"]), data)

    def test_short_and_wrong_flags_rejected(self):
        with self.assertRaises(ValueError):
            module.extract(sample(payload=bytes(range(121)), truncate=True), 771, 769, 2, 0x4c)
        with self.assertRaises(ValueError):
            module.extract(sample(flags=1), 771, 769, 2, 0x4c)

    def test_non_target_ignored(self):
        with self.assertRaises(ValueError):
            module.extract(sample(address=0x48), 771, 769, 2, 0x4c)


if __name__ == "__main__":
    unittest.main()
