"""Read saved Nsight evidence without launching a game or modifying captures.

Systems annotations are selection windows, NOT exclusive pass timings. Trim
their edges to test sensitivity to adjoining work. Percentiles describe 1-kHz
counter samples, not frame-time percentiles. GPU metrics are device-wide.

The Graphics 2026.3.1 CSV has misaligned middle headings in this capture. Read
only the verified identity/resource prefix and fixed stall-count suffix, then
check every reconstructed sample share against the exported Samples column.
"""
import argparse
import bisect
import csv
import hashlib
import json
import math
from pathlib import Path
import sqlite3
import statistics
import struct


STALLS = "SELECT NOTSEL THDBAR SLEEP BRANCH WAIT NOINST DISPCH MATHTH SHRTSB MIOT DRAIN MEMBAR LGSB TEXTHR LGTHR MISC".split()
METRICS = [
    "GR Engine Active [Throughput %]",
    "SM Throughput [Throughput %]",
    "SM Issue Stage Throughput [Throughput %]",
    "RTCORE Throughput [Throughput %]",
    "VRAM Total Bandwidth [Throughput %]",
    "L1TEX Throughput [Throughput %]",
    "L1TEX Texture Filter-Stage Throughput [Throughput %]",
    "VidL2 Sector Hit-Rate [Ratio %]",
    "Sync CS SM Warps [Occupancy %]",
    "CS Register Allocation (Sync) [Occupancy %]",
    "Predicated-On Active Threads Per Warp [Threads/Warp]",
    "Predicated-On Active Threads Per Warp [Coherence]",
    "Threads Launched Per Warp [Threads/Warp]",
    "GCC Inst Bandwidth [Throughput %]",
]


def sha256(path):
    with open(path, "rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def decode(value, size, kind):
    # SQLite's INTEGER column contains the raw payload, including IEEE bits
    # for floating-point fields and sign extension for unsigned fields.
    if not 1 <= size <= 8:
        raise ValueError(f"Unsupported metric size: {size}")
    bits = int(value) & ((1 << (8 * size)) - 1)
    if kind == "Unsigned":
        return bits
    if kind == "Signed":
        return bits - (1 << (8 * size)) if bits >> (8 * size - 1) else bits
    if (kind, size) in (("Float", 4), ("Double", 8)):
        return struct.unpack("<f" if size == 4 else "<d", bits.to_bytes(size, "little"))[0]
    raise ValueError(f"Unsupported metric representation: {kind}/{size}")


def percentile(values, fraction):
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def summary(values):
    if not values or not all(math.isfinite(x) for x in values):
        raise ValueError("Empty/nonfinite measurement selection")
    return dict(samples=len(values), mean=statistics.fmean(values),
                p10=percentile(values, .1), median=statistics.median(values),
                p90=percentile(values, .9))


def in_windows(timestamp, ranges, starts):
    index = bisect.bisect_right(starts, timestamp) - 1
    return index >= 0 and timestamp < ranges[index][1]


def interval_union_ns(ranges):
    end = None
    total = 0
    for start, finish in sorted(ranges):
        total += max(0, finish - max(start, end if end is not None else start))
        end = max(finish, end if end is not None else finish)
    return total


def systems_report(path, pid):
    conn = sqlite3.connect(path.resolve().as_uri() + "?mode=ro", uri=True)
    try:
        ranges = conn.execute("""
            SELECT v.start,v.end FROM VULKAN_WORKLOAD v
            JOIN StringIds s ON s.id=v.textId
            WHERE s.value='VQ3E Path tracing'
              AND ((v.globalTid >> 24) & 16777215)=? ORDER BY v.start
        """, (pid,)).fetchall()
        if not ranges or any(e <= s for s, e in ranges):
            raise ValueError("Missing/invalid game-owned tracing selection windows")
        if any(b[0] < a[1] for a, b in zip(ranges, ranges[1:])):
            raise ValueError("Tracing selection windows overlap")
        first, last = ranges[0][0], ranges[-1][1]
        selections = {}
        for key, trim in (("full", 0), ("middle_80_percent", .1), ("middle_50_percent", .25)):
            spans = [(s + (e-s)*trim, e - (e-s)*trim) for s, e in ranges]
            selections[key] = (spans, [s for s, _ in spans])
        metrics = {}
        cadence = None
        for name in METRICS:
            info = conn.execute("""
                SELECT m.typeId,m.metricId,f.size,t.name
                FROM TARGET_INFO_GPU_METRICS m
                JOIN GENERIC_EVENT_TYPE_FIELDS f
                  ON f.typeId=m.typeId AND f.fieldIdx=m.metricId
                JOIN StringIds n ON n.id=f.fieldNameId AND n.value=m.metricName
                JOIN ENUM_NSYS_GENERIC_EVENT_FIELD_TYPE t ON t.id=f.type
                WHERE m.metricName=?
            """, (name,)).fetchall()
            if len(info) != 1:
                raise ValueError(f"Missing/ambiguous metric metadata: {name}")
            type_id, metric_id, size, kind = info[0]
            readings = [(t, decode(v, size, kind)) for t, v in conn.execute(
                "SELECT timestamp,value FROM GPU_METRICS WHERE typeId=? AND metricId=? ORDER BY timestamp",
                (type_id, metric_id))]
            # Selected metrics have no display multiplier. Do NOT extend this
            # list to GB/s, MHz or TPC averages without applying the captured
            # metric-set YAML's multiplier (which SQLite has not applied).
            metrics[name] = {key: summary([v for t, v in readings if in_windows(t, spans, starts)])
                             for key, (spans, starts) in selections.items()}
            if cadence is None:
                cadence = summary([(b[0]-a[0])/1e6 for a, b in zip(readings, readings[1:])])
        waits = conn.execute("""
            SELECT max(v.start,?),min(v.end,?) FROM VULKAN_API v
            JOIN StringIds s ON s.id=v.nameId
            WHERE s.value='vkWaitForFences' AND v.start<? AND v.end>?
              AND ((v.globalTid >> 24) & 16777215)=?
        """, (first, last, last, first, pid)).fetchall()
        api = conn.execute("""
            SELECT s.value,count(*),sum(min(v.end,?)-max(v.start,?))/1e6
            FROM VULKAN_API v JOIN StringIds s ON s.id=v.nameId
            WHERE v.start<? AND v.end>? AND ((v.globalTid >> 24) & 16777215)=?
            GROUP BY s.value ORDER BY 3 DESC LIMIT 12
        """, (last, first, last, first, pid)).fetchall()
        return dict(path=str(path), sha256=sha256(path), pid=pid,
                    window_count=len(ranges), selected_frame_span_ms=(last-first)/1e6,
                    first_window_ns=first, last_window_ns=last,
                    annotation_duration_ms=summary([(e-s)/1e6 for s, e in ranges]),
                    counter_cadence_ms=cadence, metrics=metrics,
                    cpu_fence_wait_union_ms=interval_union_ns(waits)/1e6,
                    cpu_fence_wait_span_percent=100*interval_union_ns(waits)/(last-first),
                    cpu_api_ms=api,
                    warnings=conn.execute("SELECT text FROM DIAGNOSTIC_EVENT").fetchall(),
                    limitations=["Device-wide counters; other applications can contribute.",
                                 "Annotation windows are not exclusive execution-complete pass timings.",
                                 "Counter sample averages, not per-frame FPS acceptance measurements."])
    finally:
        conn.close()


def shader_report(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.reader(stream))
    if rows[0][-21:-4] != STALLS or rows[0][5:9] != ["File Name", "Samples", "# Warp", "# Reg"]:
        raise ValueError("Unrecognized Nsight shader export layout")
    parsed = []
    for row in rows[1:]:
        counts = dict(zip(STALLS, map(int, row[-21:-4])))
        parsed.append(dict(name=row[2], file=row[5], shader_hash=row[3],
                           sample_share=float(row[6]), max_warps=row[7], registers=row[8],
                           shared_memory_bytes=row[9], group_size=row[10],
                           samples=sum(counts.values()), stall_counts=counts))
    total = sum(row["samples"] for row in parsed)
    if not total:
        raise ValueError("Empty shader sample export")
    for row in parsed:
        if abs(row["samples"]/total - row["sample_share"]) > 1e-8:
            raise ValueError("Stall counts do not reproduce the exported shader sample share")
        row["stall_percent"] = {k: 100*v/row["samples"] for k, v in row["stall_counts"].items()} if row["samples"] else {}
    return dict(path=str(path), sha256=sha256(path), total_samples=total, shaders=parsed,
                limitations=["Shader sample shares are not exclusive GPU time or FPS shares.",
                             "Ignored malformed middle headings and unavailable zero execution counters."])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--systems", type=Path, required=True)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--shaders", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = dict(systems=systems_report(args.systems, args.pid), graphics=shader_report(args.shaders))
    payload = json.dumps(result, indent=2)
    if args.output:
        # Never replace evidence or a pre-existing analysis.
        with args.output.open("x", encoding="utf-8") as stream:
            stream.write(payload + "\n")
    else:
        print(payload)


if __name__ == "__main__":
    main()
