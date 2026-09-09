"""Offline tests for saved-capture analysis; no NVIDIA tools/GPU required."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location("analysis", Path(__file__).resolve().parents[1]/"tools/pt-analyze-nsight.py")
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


class AnalysisTests(unittest.TestCase):
    def test_float_bits_are_not_integers(self):
        bits = struct.unpack("<I", struct.pack("<f", 14.5))[0]
        self.assertEqual(analysis.decode(bits, 4, "Float"), 14.5)

    def test_unsigned_sign_extension(self):
        self.assertEqual(analysis.decode(-1344967296, 4, "Unsigned"), 2950000000)

    def test_signed(self):
        self.assertEqual(analysis.decode(255, 1, "Signed"), -1)

    def test_bad_type(self):
        with self.assertRaises(ValueError):
            analysis.decode(1, 1, "Float")

    def test_half_open_selection(self):
        ranges = [(10, 20), (30, 40)]
        self.assertEqual([analysis.in_windows(t, ranges, [10, 30]) for t in [9, 10, 19, 20, 29, 30, 40]],
                         [False, True, True, False, False, True, False])

    def test_overlapping_waits_count_once(self):
        self.assertEqual(analysis.interval_union_ns([(10, 20), (12, 15), (18, 25), (30, 35)]), 20)

    def test_summary(self):
        result = analysis.summary([1, 2, 3, 4])
        self.assertEqual(result["mean"], 2.5)
        self.assertEqual(result["median"], 2.5)
        self.assertAlmostEqual(result["p10"], 1.3)

    def test_empty_samples_rejected(self):
        with self.assertRaises(ValueError):
            analysis.summary([])


if __name__ == "__main__":
    unittest.main()
