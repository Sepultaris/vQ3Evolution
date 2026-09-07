"""Coarse live/frozen flame-animation check after a successful pt_effects.cfg run.

Not a transmission, physical-lighting, or general material-correctness proof.
Requires NumPy/Pillow and fresh captures from the fixed q3dm1 test camera.
"""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("screenshots", type=Path)
    args = parser.parse_args()
    images = [np.asarray(Image.open(args.screenshots / (name + ".jpg")).convert("RGB"), dtype=float)
              for name in ("pt_flame_live_0", "pt_flame_live_1",
                           "pt_flame_frozen_0", "pt_flame_frozen_1")]
    if any(image.shape != images[0].shape for image in images):
        raise SystemExit("FAIL: capture dimensions differ")
    reference = images[3]
    r, g, b = np.moveaxis(reference, -1, 0)
    mask = (r > 100) & (r > g * 1.02) & (g > b * 1.3)
    height, width = mask.shape
    mask[:, :int(width * .3)] = False
    mask[:, int(width * .7):] = False
    mask[:int(height * .05)] = False
    mask[int(height * .9):] = False
    if mask.mean() < .005:
        raise SystemExit("FAIL: insufficient visible flame pixels; check the test camera")
    live = np.abs(images[0][mask] - images[1][mask]).mean()
    frozen = np.abs(images[2][mask] - images[3][mask]).mean()
    print(f"Flame mask: {mask.mean():.1%}; live change {live:.2f}/255; frozen change {frozen:.2f}/255")
    if live < 1 or live < frozen * 2:
        raise SystemExit("FAIL: live material animation was not distinguishable from frozen residual noise")
    print("PASS: visible animated emitter changes live and stabilizes in reference mode")


if __name__ == "__main__":
    main()
