"""Coarse fixed-view denoiser regression; not a motion/ghosting quality test.

Run after pt_reconstruction.cfg, passing reference, raw and filtered JPEG paths.
The game keeps simulating, so small animated-object differences are expected.
"""
import argparse

import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference")
    parser.add_argument("raw")
    parser.add_argument("filtered")
    args = parser.parse_args()
    images = [np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)
              for path in (args.reference, args.raw, args.filtered)]
    reference, raw, filtered = images
    if any(im.shape != reference.shape for im in images):
        raise SystemExit("FAIL: capture dimensions differ")
    luma = reference @ np.array([0.2126, 0.7152, 0.0722])
    mask = (luma > 10) & (luma < 220)
    if np.count_nonzero(mask) < luma.size * 0.05:
        raise SystemExit("FAIL: insufficient exposed reference pixels")
    raw_mse = np.mean((raw[mask] - reference[mask]) ** 2)
    filtered_mse = np.mean((filtered[mask] - reference[mask]) ** 2)
    print(f"Raw display-space MSE: {raw_mse:.2f}")
    print(f"Filtered display-space MSE: {filtered_mse:.2f}")
    if raw_mse <= 0 or filtered_mse >= raw_mse * 0.95:
        raise SystemExit("FAIL: filtering did not reduce fixed-view error by at least 5%")
    print(f"PASS: fixed-view error reduced by {1 - filtered_mse / raw_mse:.1%}")


if __name__ == "__main__":
    main()
