"""Coarse integration check for the fixed-camera pt_lighting.cfg captures.

Requires Pillow and NumPy. It checks a lighting difference, not integrator accuracy.
Run: python tests/pt_reference_check.py <pt_direct.jpg> <pt_bounce.jpg>
"""
import argparse

import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("direct")
    parser.add_argument("bounced")
    args = parser.parse_args()
    direct = np.asarray(Image.open(args.direct).convert("RGB"), dtype=np.float64)
    bounced = np.asarray(Image.open(args.bounced).convert("RGB"), dtype=np.float64)
    if direct.shape != bounced.shape:
        raise SystemExit("FAIL: capture dimensions differ")
    weights = np.array([0.2126, 0.7152, 0.0722])
    direct_luma = direct @ weights
    bounced_luma = bounced @ weights
    # Exclude near-black/unexposed and saturated areas from this coarse check.
    mask = (direct_luma > 10) & (direct_luma < 160)
    if np.count_nonzero(mask) < direct_luma.size * 0.05:
        raise SystemExit("FAIL: insufficient exposed world pixels")
    difference = bounced_luma[mask] - direct_luma[mask]
    print(f"Mean indirect-light increase: {difference.mean():.2f} / 255")
    print(f"Pixels brighter by >2 levels: {np.mean(difference > 2):.1%}")
    if difference.mean() < 3 or np.mean(difference > 2) < 0.5:
        raise SystemExit("FAIL: multi-bounce image did not show the expected indirect-light increase")
    print("PASS: fixed-scene multi-bounce lighting differs from direct-only lighting")


if __name__ == "__main__":
    main()
