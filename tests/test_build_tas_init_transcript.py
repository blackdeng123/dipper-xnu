#!/usr/bin/env python3
"""Check transaction pairing and rejection of incomplete private captures."""
import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1] / "tools/build-tas-init-transcript.py"
spec = importlib.util.spec_from_file_location("build_tas_init_transcript", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class TranscriptTest(unittest.TestCase):
    def setUp(self):
        self.init = {"format": 1, "bus": 2, "address": 0x4c, "sample_count": 2,
                     "events": [{"event": "write", "message": 0, "time_ns": 1000, "data_hex": "03"},
                                {"event": "reply", "message": 1, "time_ns": 3000, "data_hex": "a0"}]}
        self.cal = ("# entries-in-buffer/entries-written: 1/1 #P:8\n"
                    "task 1.000001: i2c_write: i2c-2 #0 a=04c f=0000 l=2 [00-00]\n")

    def test_pairs_read_and_appends_calibration(self):
        data, count = module.build(self.init, self.cal)
        self.assertEqual(count, 2)
        self.assertEqual(data[:8], b"MI8TAS1\0")

    def test_unpaired_reply_and_truncated_trace_fail(self):
        self.init["events"][1]["message"] = 0
        with self.assertRaises(ValueError):
            module.build(self.init, self.cal)
        self.init["events"][1]["message"] = 1
        with self.assertRaises(ValueError):
            module.build(self.init, self.cal.replace("1/1", "1/2"))
        with self.assertRaises(ValueError):
            module.build(self.init, self.cal.replace("l=2", "l=3"))


if __name__ == "__main__":
    unittest.main()
