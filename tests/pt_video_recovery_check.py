"""Check injected WSI recovery, live frames, and exact safe-boundary restarts.

Run the separate raw Vulkan validation gate as well. These injected results
exercise real engine cleanup but do not claim OS-driven surface loss coverage.
"""
import argparse
import re
from pathlib import Path

CASES = (
    "acquire-out-of-date", "acquire-surface-lost", "acquire-timeout",
    "acquire-not-ready", "acquire-suboptimal", "present-out-of-date",
    "present-surface-lost", "present-suboptimal",
)


def check(text, nvidia=False):
    if "PT_RECOVERY_COMPLETE" not in text or "SDL audio shut down." not in text:
        raise ValueError("incomplete recovery test or shutdown")
    if re.findall(r"^PT_RECOVERY_BEGIN_(\d+)$", text, re.M) != list(map(str, range(8))) or \
            re.findall(r"^PT_RECOVERY_END_(\d+)$", text, re.M) != list(map(str, range(8))):
        raise ValueError("missing/repeated recovery case")
    for i, case in enumerate(CASES):
        block = text.split(f"PT_RECOVERY_BEGIN_{i}\n", 1)[1].split(f"PT_RECOVERY_END_{i}\n", 1)[0]
        if f"Vulkan recovery TEST armed: {case}" not in block or \
                f"Vulkan recovery TEST {case.split('-')[0]} result:" not in block:
            raise ValueError(f"{case}: injection was not consumed")
        expected = 0 if case in ("acquire-timeout", "acquire-not-ready") else 1
        recovery = "Vulkan: deferred swapchain recovery ("
        boundary = "Video recovery: restarting at engine frame boundary"
        if block.count(recovery) != expected or block.count(boundary) != expected:
            raise ValueError(f"{case}: wrong number of deferred requests/restarts")
        if expected and not (block.index(recovery) < block.index(boundary) < block.index("RE_Shutdown( 1 )")):
            raise ValueError(f"{case}: teardown preceded safe-boundary recovery")
        frames = re.search(r"Path tracer: active (\d+), frame (\d+)", block)
        if not frames or frames[1] != "1" or int(frames[2]) < 30:
            raise ValueError(f"{case}: path tracing did not resume")
        if nvidia and expected and ("NVIDIA DLSS Neural Rendering evaluation active" not in block or
                "NVIDIA DLSS Frame Generation active (2 frames presented)" not in block):
            raise ValueError(f"{case}: NVIDIA features did not resume")
        print(f"PASS: {case}; {expected} deferred restart(s), live frame {frames[2]}")
    if text.count("Destroy logical device") != 7:
        raise ValueError("expected exactly seven device lifetimes")
    print("PASS: all eight recovery cases; no restart for temporary acquisition unavailability")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--nvidia", action="store_true")
    args = parser.parse_args()
    try:
        check(args.log.read_text(errors="replace"), args.nvidia)
    except ValueError as error:
        raise SystemExit(f"FAIL: {error}")
