"""Check actual stock-map dielectric classification and underwater medium setup."""
import argparse
import re
from pathlib import Path
import numpy as np
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("log", type=Path)
parser.add_argument("screenshots", type=Path)
args = parser.parse_args()
log = args.log.read_text(errors="replace")
views = re.findall(r"\((-?\d+) (-?\d+) (-?\d+)\) : (-?\d+)", log)
assert len(views) == 3, f"Missing camera checkpoints: {views}"
for view, expected in zip(views, ((640, 900, 70, 90), (640, 1024, -50, 90), (0, -968, 132, 0))):
    # setviewpos imparts a short forward impulse; allow its noclip settling,
    # but reject a capture taken at the spawn instead of the scripted surface.
    assert max(abs(int(view[i])-expected[i]) for i in range(3)) < 160 and \
        abs(int(view[3])-expected[3]) < 2, f"Camera command did not settle: {view}, target {expected}"
states = re.findall(r"transport: (\d+) dielectrics, \d+ PBR mapped, (\d+) BSP water volumes, camera IOR ([\d.]+)", log)
assert len(states) == 3, f"Expected three completed transport checkpoints, got {states}"
assert int(states[0][0]) > 0 and int(states[0][1]) > 0 and float(states[0][2]) == 1
assert int(states[1][1]) > 0 and abs(float(states[1][2])-1.333) < .001
assert int(states[2][0]) > 0 and float(states[2][2]) == 1
for name in ("water_above", "water_below", "stock_glass"):
    assert f"Wrote screenshots/pt_{name}.jpg" in log
    pixels = np.asarray(Image.open(args.screenshots / f"pt_{name}.jpg"), dtype=float)/255
    assert pixels.mean() > .003 and pixels.std() > .01, f"Empty transport output: {name}"
for name, expected in (("water_mask", (1.333/3, 0, 1)), ("stock_glass_mask", (.5, 1, 0))):
    pixels = np.asarray(Image.open(args.screenshots / f"pt_{name}.jpg"), dtype=float)/255
    area = (np.max(np.abs(pixels-np.array(expected)), axis=2) < .06).mean()
    assert area > .01, f"No substantial visible dielectric area in {name}: {area}"
    print(f"{name}: {area:.1%} dielectric coverage")
print("PASS: stock glass/water classification, above/below-water camera medium and nonempty transport")
