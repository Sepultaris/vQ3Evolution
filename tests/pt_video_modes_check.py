"""Check actual output extents, mode preservation, and live frames after changes.

This checks pt_video_modes.cfg. It is not an Alt+Enter/drag-resize input test.
Run pt_validation_check.py separately against all five raw instance lifetimes.
"""
import argparse
import re
from pathlib import Path

MODES = ("WINDOW", "FULLSCREEN", "FULLSCREEN_RECOVERED", "WINDOW_RETURN",
         "ULTRAWIDE", "MAP_CHANGED", "MENU", "COMPLETE")


def check(text, neural_rendering=False, frame_generation=False):
    if re.findall(r"^PT_MODE_(\w+)$", text, re.M) != list(MODES) or \
            "SDL audio shut down." not in text:
        raise ValueError("incomplete mode sequence or shutdown")
    sizes = [tuple(map(int, match)) for match in re.findall(
        r"Vulkan drawable: (\d+) x (\d+), fullscreen: (\d+)", text)]
    if len(sizes) != 5 or sizes[0] != (960, 540, 0) or \
            sizes[1][2] != 1 or sizes[2] != sizes[1] or \
            sizes[3] != sizes[0] or sizes[4] != (1280, 540, 0):
        raise ValueError("fullscreen recovery/windowed return/ultrawide extent mismatch")
    targets = [tuple(map(int, match)) for match in re.findall(
        r"VQ3 Evolution render targets: \d+x\d+ scene, (\d+)x(\d+) output", text)]
    if targets != [size[:2] for size in sizes]:
        raise ValueError("render targets do not match swapchain extents")
    for i, mode in enumerate(MODES[:6]):
        block = text.split(f"PT_MODE_{mode}\n", 1)[1].split(f"PT_MODE_{MODES[i+1]}\n", 1)[0]
        frames = re.search(r"Path tracer: active (\d+), frame (\d+)", block)
        if not frames or frames[1] != "1" or int(frames[2]) < 30:
            raise ValueError(f"{mode}: no live path tracing after change")
        if i < 5 and not re.search(r'"r_fullscreen" is:"' + str(sizes[i][2]) + r'\^7"', block):
            raise ValueError(f"{mode}: requested fullscreen setting was changed")
    boundary = "Video recovery: restarting at engine frame boundary"
    if text.count(boundary) != 1 or text.count("Destroy logical device") != 5:
        raise ValueError("unexpected recovery loop or incomplete device lifetimes")
    if neural_rendering and text.count("NVIDIA DLSS Neural Rendering evaluation active") != 5:
        raise ValueError("Neural Rendering did not resume in every lifetime")
    if frame_generation and text.count("NVIDIA DLSS Frame Generation active (2 frames presented)") != 5:
        raise ValueError("generated frames were not reported in every lifetime")
    print("PASS: window/fullscreen/recovery/window return/ultrawide/map/menu; matching drawable and output extents")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--neural-rendering", action="store_true")
    parser.add_argument("--frame-generation", action="store_true")
    args = parser.parse_args()
    try:
        check(args.log.read_text(errors="replace"), args.neural_rendering, args.frame_generation)
    except ValueError as error:
        raise SystemExit(f"FAIL: {error}")
