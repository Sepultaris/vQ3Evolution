"""GPU mirror correspondence regression; run after pt_mirror.cfg in isolation."""
import argparse
from pathlib import Path
import numpy as np
from pt_temporal_check import read_image, debug_masks


def check(directory):
    centers = []
    for name in ("static", "moving", "turn"):
        rgb = read_image(directory, "pt_mirror_"+name)
        green, red, blue, cyan = debug_masks(rgb)
        target = blue | cyan
        reuse = cyan.sum()/max(target.sum(),1)
        print(f"{name}: reflected target {target.mean():.2%}, target reuse {reuse:.1%}, background reuse {green.mean():.1%}")
        if target.mean()<.003 or reuse<.7 or green.mean()<.15:
            raise SystemExit("FAIL: missing/rejected virtual reflected target history")
        centers.append(np.where(target)[1].mean())
    if abs(centers[1]-centers[0])<5:
        raise SystemExit("FAIL: target did not move in the mirror")
    a, b = (read_image(directory, "pt_mirror_beauty_"+s) for s in ("a","b"))
    if np.mean(abs(a-b))<.5:
        raise SystemExit("FAIL: reflected radiance did not change with target motion")
    for name in ("removed", "recovered"):
        green, red, blue, cyan = debug_masks(read_image(directory,"pt_mirror_"+name))
        if (blue|cyan).mean()>.002 or green.mean()<.15:
            raise SystemExit("FAIL: stale reflected target survived removal")
    print("PASS: off-camera target motion, mirror turn, removal and recovery")


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("screenshots",type=Path)
    check(parser.parse_args().screenshots)
