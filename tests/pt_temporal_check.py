"""Coarse integration checks for the native path-tracing temporal pass.

Requires NumPy/Pillow. Run only after the corresponding isolated game test exits
successfully, with fresh captures. These checks are not a general ghosting,
reflection quality, or unbiased-estimator proof.
"""
import argparse
import re
from pathlib import Path

import numpy as np
from PIL import Image


def read_image(directory, name):
    return np.asarray(Image.open(directory / (name + ".jpg")).convert("RGB"),
                      dtype=np.float64)


def debug_masks(rgb):
    r, g, b = np.moveaxis(rgb, -1, 0)
    # Tolerate JPEG compression and dimmer green for shorter accepted history.
    return ((g > 55) & (g > r + 40) & (g > b + 40),
            (r > 180) & (g < 65) & (b < 65),
            (b > 180) & (r < 65) & (g < 65),
            (g > 150) & (b > 180) & (r < 65))


def check_quality(directory):
    reference = read_image(directory, "pt_temporal_reference")
    spatial = np.stack([read_image(directory, f"pt_spatial_{i}") for i in range(4)])
    temporal = np.stack([read_image(directory, f"pt_history_{i}") for i in range(4)])
    debug = read_image(directory, "pt_temporal_mask")
    if spatial.shape != temporal.shape or spatial.shape[1:] != reference.shape or debug.shape != reference.shape:
        raise SystemExit("FAIL: capture dimensions differ")
    green, red, _, _ = debug_masks(debug)
    world = green | red
    # Erode the world mask to omit mixed silhouette pixels and moving objects.
    padded = np.pad(world, 2, constant_values=False)
    world = np.logical_and.reduce([
        padded[y:y + world.shape[0], x:x + world.shape[1]]
        for y in range(5) for x in range(5)])
    luma = reference @ np.array([0.2126, 0.7152, 0.0722])
    mask = world & (luma > 10) & (luma < 220)
    if mask.mean() < 0.05 or green.mean() < 0.5:
        raise SystemExit("FAIL: insufficient exposed/reprojected world pixels")
    spatial_mse = np.mean((spatial[:, mask] - reference[mask]) ** 2)
    temporal_mse = np.mean((temporal[:, mask] - reference[mask]) ** 2)
    spatial_variance = np.var(spatial[:, mask], axis=0).mean()
    temporal_variance = np.var(temporal[:, mask], axis=0).mean()
    print(f"World mask: {mask.mean():.1%}; reused history: {green.mean():.1%}")
    print(f"Mean display-space MSE: spatial {spatial_mse:.2f}, temporal {temporal_mse:.2f}")
    print(f"Four-frame variance: spatial {spatial_variance:.2f}, temporal {temporal_variance:.2f}")
    if temporal_mse >= spatial_mse * 0.95 or temporal_variance >= spatial_variance * 0.95:
        raise SystemExit("FAIL: temporal history did not improve both measures by at least 5%")
    print(f"PASS: error reduced {1 - temporal_mse / spatial_mse:.1%}; "
          f"variance reduced {1 - temporal_variance / spatial_variance:.1%}")


def check_motion(directory, log):
    for name, minimum in (("static", 0.7), ("turn", 0.35),
                          ("move", 0.35), ("settled", 0.5)):
        green, red, blue, cyan = debug_masks(read_image(directory, "pt_temporal_" + name))
        world_fraction = (green | red).mean()
        reused = green.mean() / max(world_fraction, 1e-9)
        print(f"{name}: world reuse {reused:.1%} ({world_fraction:.1%} eligible image), "
              f"object reuse {cyan.mean():.1%}, rejected objects {blue.mean():.1%}")
        # Animated/emissive panels keep their color, so turning toward those
        # panels substantially reduces eligible world area. Still require a
        # meaningful mask and test reuse only within explicitly coded guides.
        if world_fraction < 0.05 or reused < minimum:
            raise SystemExit(f"FAIL: too little history survived {name}")
        if name in ("static", "turn") and cyan.mean() < 0.005:
            raise SystemExit("FAIL: tracked first-person geometry did not reuse history")
        if name == "turn" and red.mean() < 0.001:
            raise SystemExit("FAIL: no newly revealed pixels rejected history during turn")
    causes = [tuple(map(int, match)) for match in re.findall(
        r"Temporal reset causes: renderer (\d+), invalid (\d+), lights (\d+), time (\d+), camera/settings (\d+)",
        log.read_text(errors="replace"))]
    if len(causes) != 4:
        raise SystemExit("FAIL: expected four diagnostic checkpoints from pt_temporal.cfg")
    if causes[0] != causes[1]:
        raise SystemExit("FAIL: ordinary camera turning caused a global history reset")
    if causes[2][4] != causes[1][4] + 1:
        raise SystemExit("FAIL: scripted camera cut did not register one camera reset")
    if causes[3][2] != causes[2][2]:
        raise SystemExit("FAIL: firing discarded global lighting history")
    updates = [int(n) for n in re.findall(r"reconstruction: (\d+) local light updates", log.read_text(errors="replace"))]
    if len(updates) != 4 or updates[3] <= updates[2]:
        raise SystemExit("FAIL: firing did not reach the local lighting comparison")
    matched = [int(value) for value in re.findall(r"Path tracing object motion: (\d+) matched", log.read_text(errors="replace"))]
    if len(matched) != 4 or min(matched) < 1:
        raise SystemExit("FAIL: missing stable object correspondence at a checkpoint")
    print("PASS: world/object reprojection, disocclusion, camera cut and local game-light updates")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("screenshots", type=Path)
    parser.add_argument("--mode", choices=("quality", "motion"), default="quality")
    parser.add_argument("--log", type=Path, help="Fresh qconsole.log from motion test")
    args = parser.parse_args()
    if args.mode == "quality":
        check_quality(args.screenshots)
    else:
        if args.log is None:
            parser.error("--mode motion requires --log")
        check_motion(args.screenshots, args.log)


if __name__ == "__main__":
    main()
