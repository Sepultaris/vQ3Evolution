"""Verify an attended, completed pt_fg_focus_recovery.cfg run.

Require sustained foreground interpolation before and after a measured
background interval. A single final focused snapshot is not recovery proof.
Vulkan validation is a separate gate; successful presentation cannot waive it.
"""
import argparse
import re
from pathlib import Path

from pt_frame_generation_check import STATS


def check(text):
    expected = [("STATIC" if i < 18 else "MOVING", str(i)) for i in range(20)]
    headers = list(re.finditer(r"^PT_FOCUS_(STATIC|MOVING)_(\d+)$", text, re.M))
    if [h.groups() for h in headers] != expected or "PT_FOCUS_COMPLETE" not in text or \
            "Streamline shutdown complete before device destruction" not in text:
        raise ValueError("incomplete focus-recovery test")
    if "VK_PRESENT_MODE_IMMEDIATE_KHR mode" not in text or \
            "NVIDIA DLSS Neural Rendering evaluation active" not in text:
        raise ValueError("missing required presentation mode or Neural Rendering activation")
    snapshots = []
    for i, header in enumerate(headers):
        end = headers[i + 1].start() if i + 1 < len(headers) else len(text)
        block = text[header.end():end]
        stats = STATS.search(block)
        window = re.search(r"NVIDIA window state: focused (\d+), minimized (\d+)", block)
        history = re.search(r"NVIDIA FG query history: unfocused (\d+), minimized (\d+), failed (\d+)", block)
        if not all((stats, window, history)):
            raise ValueError(f"sample {i}: missing counters")
        enabled, active, queries, presented, peak, result = map(int, stats.groups()[:6])
        unfocused, minimized, failed = map(int, history.groups())
        focused, window_minimized = map(int, window.groups())
        if (enabled, active, result, int(stats[7], 16), failed, minimized, window_minimized) != (1, 1, 0, 0, 0, 0, 0):
            raise ValueError(f"sample {i}: inactive feature, SDK failure, or minimized window")
        snapshots.append((queries, presented, unfocused, focused, peak))
    foreground, background = [], []
    for i, (previous, current) in enumerate(zip(snapshots, snapshots[1:]), 1):
        frames, presents, unfocused = (current[j] - previous[j] for j in range(3))
        if frames < 20 or presents < 0 or not 0 <= unfocused <= frames:
            raise ValueError(f"interval {i}: insufficient frames or reset counters")
        state = "transition"
        if previous[3] == current[3] == 1 and unfocused == 0:
            if presents < frames * 1.25 or current[4] < 2:
                raise ValueError(f"interval {i}: foreground interpolation stopped")
            foreground.append(i)
            state = "foreground"
        elif previous[3] == current[3] == 0 and unfocused == frames:
            if presents > frames * 1.25:
                raise ValueError(f"interval {i}: background interpolation did not pause")
            background.append(i)
            state = "background"
        print(f"{i:2}: {state:10} {presents:3} presentations / {frames:3} rendered ({presents / frames:.2f}x)")
    if not background or not foreground or foreground[0] >= background[0] or foreground[-1] <= background[-1]:
        raise ValueError("did not measure foreground -> background -> foreground recovery")
    if not all(i in foreground for i in (18, 19)):
        raise ValueError("final moving-camera intervals did not sustain foreground interpolation")
    print("PASS: focus loss paused interpolation; recovery sustained it, including both moving-camera intervals")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    try:
        check(args.log.read_text(errors="replace"))
    except ValueError as error:
        raise SystemExit(f"FAIL: {error}")
