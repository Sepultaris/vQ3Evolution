"""Summarize native GPU timestamp/real frame-time captures, with optional comparison."""
import argparse
import re
import statistics
from pathlib import Path

FIELDS = ("frame", "raster", "as", "trace", "temporal", "spatial", "post")


def summarize(path, minimum=50):
    text = path.read_text(errors="replace")
    rows = [dict((k, float(v)) for k, v in re.findall(r"(\w+)=([\d.]+)", line))
            for line in text.splitlines() if line.startswith("PT_PROFILE ")]
    # Quake executes its command buffer twice per rendered frame. Keep support
    # for the original 59-frame baseline; new captures run roughly 599 frames.
    if len(rows) < minimum or "PT_BENCH_STATIC" not in text or "PT_BENCH_TURN" not in text or \
            "Path tracer: active" not in text or any(set(r) not in (set(FIELDS), set(FIELDS) | {"guides"}) for r in rows):
        raise SystemExit(f"FAIL: {path.name}: incomplete profile ({len(rows)} frames)")
    print(f"{path.name}: {len(rows)} frames (milliseconds, no added query waits)")
    result = {}
    views = re.findall(r"\((-?\d+) (-?\d+) (-?\d+)\) : (-?\d+)", text)
    if views:
        x, y, z, yaw = map(int, views[0])
        # The game resolves the requested position out of the adjacent wall;
        # check the actual settled camera, not merely command submission.
        if abs(x+1510)>8 or abs(y-200)>8 or abs(z-50)>8 or abs(yaw)>1:
            raise SystemExit(f"FAIL: benchmark camera did not settle: {views[0]}")
        print(f"  verified initial camera: {views[0]}")
    else:
        print("  historical capture: initial camera was not logged")
    for key in FIELDS + ("guides",):
        values = sorted(row.get(key, 0) for row in rows)
        result[key] = statistics.median(values)
        print(f"  {key:9} median {result[key]:9.3f}  p95 {values[int(.95*(len(values)-1))]:9.3f}  max {max(values):9.3f}")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--min-frames", type=int, default=50)
    args = parser.parse_args()
    current = summarize(args.log, args.min_frames)
    if args.compare:
        baseline = summarize(args.compare)
        print(f"Median frame-time reduction: {(1-current['frame']/baseline['frame'])*100:.1f}%")
        print(f"Median tracing + guides speedup: {(baseline['trace']+baseline['guides'])/max(current['trace']+current['guides'], .001):.2f}x")
