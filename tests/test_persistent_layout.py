"""Check the partition and GUID guards before persistent images are built."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("persistent_builder", ROOT / "tools/build-persistent-autoboot.py")
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)


class LayoutGuardTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.old_kernel = BUILDER.KERNEL
        self.old_minimum = BUILDER.MIN_SYSTEM_BLOCKS
        BUILDER.KERNEL = self.root
        BUILDER.MIN_SYSTEM_BLOCKS = 200
        (self.root / "xnuloader").mkdir()
        (self.root / "xnu-src/xnu-8796.121.2/bsd/dev").mkdir(parents=True)
        (self.root / "userland/src").mkdir(parents=True)
        self.loader = self.root / "xnuloader/loader.c"
        self.ufs = self.root / "xnu-src/xnu-8796.121.2/bsd/dev/redwood_ufs.c"
        self.store = self.root / "userland/src/var_store.c"
        self.guid = self.root / "guid"
        self.guid.write_text("12345678-1234-5678-90ab-cdef12345678\n")
        self.loader.write_text(
            "#define BLOCK_DARWIN_FIRST 100ULL\n#define BLOCK_DARWIN_LAST 999ULL\n"
            "#define BLOCK_WRITE_FIRST 200ULL\n"
            "#define BLOCK_DARWIN_TYPE { 0x7c3457ef, 0, 0x11aa, "
            "{ 0xaa, 0x11, 0, 0x30, 0x65, 0x43, 0xec, 0xac } }\n"
            "#define BLOCK_DARWIN_UNIQ { 0x12345678, 0x1234, 0x5678, "
            "{ 0x90, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78 } }\n"
        )
        self.ufs.write_text(
            "#define UFS_DARWIN_FIRST 100ULL\n#define UFS_DARWIN_BLOCKS 900ULL\n"
            "#define UFS_WRITE_FIRST 200ULL\n"
        )
        self.store.write_text("#define STORE_AT 819200LL\n#define SLOT_BYTES 409600LL\n#define SLOTS 2\n")

    def tearDown(self):
        BUILDER.KERNEL = self.old_kernel
        BUILDER.MIN_SYSTEM_BLOCKS = self.old_minimum
        self.tmp.cleanup()

    def test_consistent_layout_is_accepted(self):
        BUILDER.check_layout(self.guid)

    def test_wrong_guid_is_rejected(self):
        self.guid.write_text("12345678-1234-5678-90ab-cdef12345679\n")
        with self.assertRaisesRegex(ValueError, "GUID"):
            BUILDER.check_layout(self.guid)

    def test_wrong_partition_type_is_rejected(self):
        self.loader.write_text(self.loader.read_text().replace("0x7c3457ef", "0x7c3457ee"))
        with self.assertRaisesRegex(ValueError, "type GUID"):
            BUILDER.check_layout(self.guid)

    def test_system_protection_cannot_be_reduced(self):
        self.loader.write_text(self.loader.read_text().replace("BLOCK_WRITE_FIRST 200", "BLOCK_WRITE_FIRST 199"))
        self.ufs.write_text(self.ufs.read_text().replace("UFS_WRITE_FIRST 200", "UFS_WRITE_FIRST 199"))
        with self.assertRaisesRegex(ValueError, "bounds"):
            BUILDER.check_layout(self.guid)

    def test_mismatched_partition_size_is_rejected(self):
        self.ufs.write_text(self.ufs.read_text().replace("BLOCKS 900", "BLOCKS 899"))
        with self.assertRaisesRegex(ValueError, "bounds"):
            BUILDER.check_layout(self.guid)

    def test_store_overlapping_system_is_rejected(self):
        self.store.write_text(self.store.read_text().replace("STORE_AT 819200", "STORE_AT 4096"))
        with self.assertRaisesRegex(ValueError, "outside"):
            BUILDER.check_layout(self.guid)

    def test_store_past_partition_end_is_rejected(self):
        self.store.write_text(self.store.read_text().replace("SLOT_BYTES 409600", "SLOT_BYTES 4194304"))
        with self.assertRaisesRegex(ValueError, "outside"):
            BUILDER.check_layout(self.guid)


if __name__ == "__main__":
    unittest.main()
