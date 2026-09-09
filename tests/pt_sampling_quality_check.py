"""Native rank-table reproducibility, spectrum and nonuniform RIS checks (NumPy)."""
import importlib.util
import re
import unittest
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]


class NoiseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        text = (ROOT / "code/renderer_vulkan/pt_blue_noise.h").read_text().split("{", 1)[1]
        cls.ranks = np.array(list(map(int, re.findall(r"\d+", text)))).reshape(64, 64)

    def test_reproducible_unique_progressive_ranks(self):
        np.testing.assert_array_equal(np.sort(self.ranks.ravel()), np.arange(4096))
        spec = importlib.util.spec_from_file_location("generator", ROOT / "tools/generate-pt-blue-noise.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        np.testing.assert_array_equal(self.ranks, module.generate())

    def test_low_frequency_suppression_at_progressive_thresholds(self):
        f = np.fft.fftfreq(64)*64
        radius = np.sqrt(f[:, None]**2 + f[None, :]**2)
        low = (radius > 0) & (radius < 6)
        for fraction in (.1, .25, .5, .75, .9):
            mask = self.ranks < int(4096*fraction)
            power = abs(np.fft.fft2(mask-mask.mean()))**2
            ratio = power[low].mean() / (4096*mask.mean()*(1-mask.mean()))
            print(f"rank threshold {fraction:.0%}: low-frequency/white power {ratio:.4f}")
            self.assertLess(ratio, .25)


if __name__ == "__main__":
    unittest.main()
