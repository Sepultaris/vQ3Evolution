"""Check complete repeated renderer teardown/recreation, not just process exit."""
import argparse
import re
from pathlib import Path


def check(path, lifecycle, neural_rendering=False, frame_generation=False, surface_lifecycle=False):
    text = path.read_text(errors="replace")
    starts = list(map(int, re.findall(r"^PT_RESTART_BEGIN_(\d+)$", text, re.M)))
    ends = list(map(int, re.findall(r"^PT_RESTART_END_(\d+)$", text, re.M)))
    if starts != list(range(12)) or ends != starts or "PT_RESTART_COMPLETE" not in text:
        raise SystemExit("FAIL: incomplete 12-restart stress test")
    active = re.findall(r"Path tracer: active (\d+), frame (\d+)", text)
    if len(active) != 12 or any(a != "1" or int(f)<10 for a,f in active):
        raise SystemExit("FAIL: a restart did not resume path-traced frames")
    if re.search(r"Streamline (?:shutdown|frame resource release|Frame Generation resource release|DLSS resource release) failed|"
                 r"NVIDIA DLSS Neural Rendering (?:shutdown|parameter release|release) failed", text):
        raise SystemExit("FAIL: NVIDIA teardown returned an error")
    if neural_rendering and text.count("NVIDIA DLSS Neural Rendering evaluation active") != 13:
        raise SystemExit("FAIL: neural rendering did not evaluate in every renderer lifetime")
    if frame_generation and text.count("NVIDIA DLSS Frame Generation active (2 frames presented)") != 13:
        raise SystemExit("FAIL: Frame Generation did not present generated frames in every renderer lifetime")
    if lifecycle:
        shutdowns = text.split("vk_shutdown()\n")[1:]
        if len(shutdowns) != 13:
            raise SystemExit(f"FAIL: expected 13 device teardowns, got {len(shutdowns)}")
        for block in shutdowns:
            markers = ["NVIDIA DLSS Neural Rendering shutdown before NGX core teardown",
                       "Streamline frame resource tags released before image destruction",
                       "Destroy vk.framebuffers", "Streamline shutdown complete before device destruction",
                       "Destroy logical device"]
            if surface_lifecycle:
                markers[3:3] = ["Destroy routed surface before Streamline shutdown",
                                "NVIDIA Neural Rendering module references released"]
            offsets = [block.find(marker) for marker in markers]
            if min(offsets)<0 or offsets != sorted(offsets):
                raise SystemExit("FAIL: external references/SDK outlived their Vulkan resources")
    print("PASS: 12 renderer restarts resumed live path tracing" +
          ("; all 13 teardowns released tags and shut down the SDK in order" if lifecycle else " (baseline)"))


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--lifecycle", action="store_true")
    parser.add_argument("--neural-rendering", action="store_true")
    parser.add_argument("--frame-generation", action="store_true")
    parser.add_argument("--surface-lifecycle", action="store_true", help="Also require routed surface and module-reference teardown order")
    args=parser.parse_args()
    if args.surface_lifecycle and not args.lifecycle:
        parser.error("--surface-lifecycle requires --lifecycle")
    check(args.log,args.lifecycle,args.neural_rendering,args.frame_generation,args.surface_lifecycle)
