import tempfile
import unittest
from pathlib import Path
from import_dkult_assets import chain, import_assets


class RelocImportTests(unittest.TestCase):
    def test_valid_internal_chain(self):
        self.assertEqual(chain(bytes.fromhex("FFFF0000"), 0, True), [0])

    def test_cycle(self):
        with self.assertRaises(ValueError):
            chain(bytes(4), 0)

    def test_truncated_chain(self):
        with self.assertRaises(ValueError):
            chain(bytes.fromhex("FFFF00"), 0)

    def test_target_outside_file(self):
        with self.assertRaises(ValueError):
            chain(bytes.fromhex("FFFF0001"), 0, True)

    def test_no_chain(self):
        self.assertEqual(chain(bytes(4), 0xFFFF), [])

    def test_dependency_mismatch_does_not_write_pack(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp)
            (source / "config.yaml").write_text('offsets:\n  main: ["3FFFC", "0000"]\n')
            (source / "main.bin").write_bytes(bytes.fromhex("FFFF0000"))
            (source / "main_reqlist.txt").write_text("END OF REQ LIST\n")
            output = source / "output"
            with self.assertRaisesRegex(ValueError, "dependency count"):
                import_assets(source, output)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
