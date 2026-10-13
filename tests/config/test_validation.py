import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "components" / "jk_rs485_native_bms"))

import validation  # noqa: E402


class ValidationTests(unittest.TestCase):
    def test_valid_two_pack(self):
        errors = validation.validate_entries(
            [
                {"address": 1, "master_id": "jk_bus", "protocol_version": "JK02_32S", "cell_count": 16},
                {"address": 2, "master_id": "jk_bus", "protocol_version": "JK02_32S", "cell_count": 16},
            ]
        )
        self.assertEqual(errors, [])

    def test_duplicate_address(self):
        errors = validation.validate_entries(
            [
                {"address": 1, "master_id": "jk_bus", "protocol_version": "JK02_32S"},
                {"address": 1, "master_id": "jk_bus", "protocol_version": "JK02_32S"},
            ]
        )
        self.assertTrue(any("duplicate" in e for e in errors))

    def test_out_of_range_address(self):
        self.assertIsNotNone(validation.validate_address(0))
        self.assertIsNotNone(validation.validate_address(16))
        self.assertIsNone(validation.validate_address(1))

    def test_missing_master(self):
        errors = validation.validate_entries([{"address": 1, "protocol_version": "JK02_32S"}])
        self.assertTrue(any("master_id" in e for e in errors))

    def test_cell_count_for_protocol(self):
        self.assertIsNotNone(validation.validate_cell_count(25, "JK02_24S"))
        self.assertIsNone(validation.validate_cell_count(24, "JK02_24S"))
        self.assertIsNone(validation.validate_cell_count(32, "JK02_32S"))
        self.assertIsNotNone(validation.validate_cell_count(33, "JK02_32S"))


if __name__ == "__main__":
    unittest.main()
