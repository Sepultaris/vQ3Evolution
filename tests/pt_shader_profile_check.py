"""Shader-clock sampling contracts and completed, saved-settings run analysis.

Clock shares are instrumented invocation-local estimates, not hardware-unit
utilization or additive frame milliseconds. Only uninstrumented baseline rows
are suitable for normal-renderer performance comparisons.
"""
import argparse
import math
from pathlib import Path
import re
import statistics
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
PHASES = ("BASELINE_A", "INSTRUMENTED", "BASELINE_B")
LIGHT_PARTS = ("emitter", "proposals", "brdf", "continuation", "lightsetup")


def values(line):
    return {key: float(value) for key, value in re.findall(r"(\w+)=([\d.eE+-]+)", line)}


def parse(text, minimum=20):
    phase = None
    seen = []
    baseline = {name: [] for name in (PHASES[0], PHASES[2])}
    diagnostic = []
    shares = []
    complete = ready = False
    for line in text.splitlines():
        if line == "PT_SHADER_COMPLETE":
            complete = True
            phase = None
        elif line in ("PT_SHADER_" + name for name in PHASES):
            phase = line.removeprefix("PT_SHADER_")
            if phase in seen:
                raise ValueError("Repeated phase")
            seen.append(phase)
        elif line.startswith("PT_SHADER_PROFILE_READY "):
            ready = True
        elif line.startswith("PT_SHADER_PROFILE_UNAVAILABLE"):
            raise ValueError("Shader-clock diagnostic unavailable")
        elif line.startswith("PT_SHADER_PROFILE "):
            row = values(line)
            expected = {"pixels", "ticks", "other", "queries", "materials", "lighting", "paths", "rays", "shadows", "hits"}
            if any(key in row for key in LIGHT_PARTS):
                expected.update(LIGHT_PARTS)
                if not all(key in row for key in LIGHT_PARTS) or abs(sum(row[key] for key in LIGHT_PARTS) - row["lighting"]) > .01:
                    raise ValueError("Lighting subcategories do not match their aggregate")
            if set(row) != expected or any(not math.isfinite(v) or v < 0 for v in row.values()):
                raise ValueError("Invalid diagnostic counters")
            if min(row["pixels"], row["ticks"], row["paths"]) <= 0:
                raise ValueError("Empty diagnostic sample")
            if abs(sum(row[k] for k in ("other", "queries", "materials", "lighting")) - 100) > .01:
                raise ValueError("Exclusive shares do not sum to 100%")
            if phase == "INSTRUMENTED":
                shares.append(row)
        elif line.startswith(("PT_PROFILE ", "PT_PROFILE_DIAGNOSTIC ")):
            row = values(line)
            if set(row) != {"frame", "raster", "as", "guides", "trace", "temporal", "spatial", "post"}:
                raise ValueError("Incomplete frame timing")
            if any(not math.isfinite(v) or v < 0 for v in row.values()):
                raise ValueError("Invalid frame timing")
            if line.startswith("PT_PROFILE_DIAGNOSTIC "):
                if phase == "INSTRUMENTED":
                    diagnostic.append(row)
            elif phase in baseline:
                baseline[phase].append(row)
    if not complete or not ready or tuple(seen) != PHASES or "Path tracer: active 1" not in text:
        raise ValueError("Incomplete run; no performance conclusion is valid")
    for name in baseline:
        baseline[name] = baseline[name][3:-3]
    shares, diagnostic = shares[3:-3], diagnostic[3:-3]
    if min([len(shares), len(diagnostic)] + [len(rows) for rows in baseline.values()]) < minimum:
        raise ValueError("Too few settled frames")
    return baseline, shares, diagnostic


def selected_pixels(width, height, frame):
    """Integer layout shared with profileBegin; return (pixel, record index)."""
    gx, gy = (width + 7) // 8, (height + 7) // 8
    tx, ty = (gx + 7) // 8, (gy + 7) // 8
    for y in range(ty):
        for x in range(tx):
            sx = x * 8 + (frame & 7) % min(8, gx - x * 8)
            sy = y * 8 + ((frame >> 3) & 7) % min(8, gy - y * 8)
            for ly in range(8):
                for lx in range(8):
                    px, py = sx * 8 + lx, sy * 8 + ly
                    if px < width and py < height:
                        yield (px, py), (y * tx + x) * 64 + ly * 8 + lx


def clock_delta(previous, now):
    mask = (1 << 32) - 1
    low = ((now & mask) - (previous & mask)) & mask
    high = ((now >> 32) - (previous >> 32) - ((now & mask) < (previous & mask))) & mask
    return low + (high << 32)


class ShaderProfileTests(unittest.TestCase):
    def test_clock_wrap(self):
        for previous, elapsed in ((123, 96), ((1 << 32) - 6, 80), ((1 << 64) - 6, 80), (0, (1 << 32) + 123)):
            now = (previous + elapsed) % (1 << 64)
            self.assertEqual(clock_delta(previous, now), elapsed)

    def test_exclusive_nested_scopes(self):
        # Root -> query -> material -> query -> root. Child time is not counted twice.
        ticks, last, category = [0] * 4, 0, 0
        for now, next_category in ((5, 1), (20, 2), (30, 1), (60, 0), (70, 0)):
            ticks[category] += clock_delta(last, now)
            last, category = now, next_category
        self.assertEqual(ticks, [15, 45, 10, 0])
        self.assertEqual(sum(ticks), 70)

    def test_partial_tiles_and_rotating_coverage(self):
        for width, height in ((1, 1), (9, 11), (65, 71), (127, 103)):
            coverage = set()
            capacity = ((width + 63) // 64) * ((height + 63) // 64) * 64
            for frame in range(64):
                selected = list(selected_pixels(width, height, frame))
                self.assertEqual(len(selected), len({record for _, record in selected}))
                self.assertTrue(all(0 <= record < capacity for _, record in selected))
                coverage.update(pixel for pixel, _ in selected)
            self.assertEqual(len(coverage), width * height)

    def test_native_1080p_allocation(self):
        selected = list(selected_pixels(1920, 1080, 63))
        self.assertEqual(len(selected), 32640)
        self.assertEqual(max(record for _, record in selected), 32639)

    def test_off_by_default_and_no_added_gpu_wait(self):
        cvars = (ROOT / "code/renderer_vulkan/tr_cvar.c").read_text()
        self.assertIn('Cvar_Get("r_pathTracingShaderProfile", "0", CVAR_CHEAT)', cvars)
        native = (ROOT / "code/renderer_vulkan/pt_shader_profile.h").read_text()
        self.assertNotIn("WaitForFences(", native)
        self.assertNotIn("WaitIdle(", native)
        self.assertIn("VK_ACCESS_HOST_READ_BIT", native)
        self.assertIn("VK_MEMORY_PROPERTY_HOST_COHERENT_BIT", native)
        self.assertIn("!r_pathTracingShaderProfile->integer || !shader_profile_initialize(brdf_reuse, map_light_cull, alias_pdf, emitter_geometry, light_loop)", native)
        self.assertIn("shader_profile_shutdown();", native)

    def test_no_random_or_quality_overrides(self):
        helper = (ROOT / "code/renderer_vulkan/shaders/pt_shader_profile.glsl").read_text()
        self.assertNotIn("randomFloat", helper)
        self.assertNotIn("rng", helper)
        config = (ROOT / "tests/pt_shader_profile.cfg").read_text()
        rendering = re.findall(r"^set (r_\w+)", config, re.M)
        self.assertEqual(set(rendering), {"r_pathTracingProfile", "r_pathTracingShaderProfile"})

    def test_detailed_lighting_layout_is_diagnostic_only(self):
        shader = (ROOT / "code/renderer_vulkan/shaders/pt_shader_profile.glsl").read_text()
        native = (ROOT / "code/renderer_vulkan/pt_shader_profile.h").read_text()
        self.assertIn("vec4 ticks[3]; uvec4 counts[4]", shader)
        self.assertIn("sizeof(pt_profile_record_t) == 112", native)
        self.assertIn("float ticks[12]; uint32_t counts[16]", native)
        self.assertIn("PT_SHADER_FOG_PROFILE", native)
        self.assertIn("(ticks[1]+ticks[10]+ticks[11])*100/total", native)
        self.assertIn("(ticks[2]+ticks[8])*100/total", native)
        self.assertIn("#ifdef PT_PROFILE_PASS", shader)

    def test_detailed_shares_reject_missing_or_inconsistent_components(self):
        good = self.fixture().replace("lighting=10", "lighting=10 emitter=1 proposals=2 brdf=3 continuation=3 lightsetup=1")
        _, shares, _ = parse(good)
        self.assertEqual(shares[0]["proposals"], 2)
        for bad in (good.replace("proposals=2", "proposals=5"), good.replace(" brdf=3", "")):
            with self.assertRaises(ValueError):
                parse(bad)

    def test_fog_scopes_are_exclusive_and_separate_from_frame_timing(self):
        base=ROOT/'code/renderer_vulkan/shaders'
        volume=(base/'pt_fog_volume.glsl').read_text()
        self.assertIn('profileEnter(8u)',volume)
        self.assertIn('profileEnter(11u)',volume)
        self.assertIn('profileEnter(9u)',(base/'pt_fog_lighting.glsl').read_text())
        self.assertIn('profileCategory==9u ? 10u:1u',(base/'pt_integrator.glsl').read_text())
        self.assertEqual(volume.count('profileEnter(previous)'),2)
        # These exclusive bins roll back into the existing aggregate parser.
        ticks=list(range(1,13));total=sum(ticks)
        groups=(ticks[0],ticks[1]+ticks[10]+ticks[11],ticks[2]+ticks[8],sum(ticks[3:8])+ticks[9])
        self.assertEqual(sum(groups),total)

    @staticmethod
    def fixture():
        normal = "PT_PROFILE frame=76 raster=.1 as=.4 guides=1 trace=62 temporal=1 spatial=2 post=6\n"
        diagnostic = normal.replace("PT_PROFILE", "PT_PROFILE_DIAGNOSTIC")
        share = "PT_SHADER_PROFILE pixels=64 ticks=1000 other=5 queries=60 materials=25 lighting=10 paths=256 rays=900 shadows=700 hits=800\n"
        return ("PT_SHADER_BASELINE_A\n" + normal * 32 + "PT_SHADER_INSTRUMENTED\n" +
                "PT_SHADER_PROFILE_READY width=1920 height=1080\n" + (share + diagnostic) * 32 +
                "PT_SHADER_BASELINE_B\n" + normal * 32 + "Path tracer: active 1\nPT_SHADER_COMPLETE\n")

    def test_complete_run_keeps_diagnostic_separate(self):
        baseline, shares, diagnostic = parse(self.fixture())
        self.assertEqual(len(baseline["BASELINE_A"]), 26)
        self.assertEqual(len(shares), 26)
        self.assertEqual(len(diagnostic), 26)

    def test_reject_incomplete_or_invalid_run(self):
        for text in (self.fixture().replace("PT_SHADER_COMPLETE", "timeout"),
                     self.fixture().replace("queries=60", "queries=70"),
                     self.fixture().replace("PT_PROFILE_DIAGNOSTIC", "PT_PROFILE"),
                     self.fixture().replace("PT_SHADER_PROFILE_READY", "PT_SHADER_PROFILE_UNAVAILABLE"),
                     self.fixture().replace("pixels=64", "pixels=0")):
            with self.assertRaises(ValueError):
                parse(text)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", nargs="?", type=Path)
    parser.add_argument('--fog-log',type=Path)
    parser.add_argument('--spirv-dis')
    args = parser.parse_args()
    if args.spirv_dis:
        for variant in ('pt_profile','pt_profile_brdf','pt_light_loop_profile','pt_light_loop_profile_brdf'):
            binary=ROOT/f'code/renderer_vulkan/shaders/Compiled/{variant}.cspv'
            text=subprocess.check_output([args.spirv_dis,str(binary)],text=True)
            assert re.search(r'OpDecorate %\w*ProfileRecord\w* ArrayStride 112\b',text),variant
            records=re.findall(r'OpName (%\w+) "ProfileRecord"',text)
            records=[record for record in records if re.search(r'OpTypeRuntimeArray '+re.escape(record)+r'\s',text)]
            assert records,variant
            for record in records:
                assert re.search(r'OpMemberDecorate '+re.escape(record)+r' 1 Offset 48\b',text),variant
        print('PASS: all four diagnostic binaries use 112-byte records, counts at byte 48')
    if args.fog_log:
        text=args.fog_log.read_text(errors='replace')
        assert 'PT_PROGRAM_COMPLETE' in text and 'PT_SHADER_PROFILE_READY' in text
        rows=[values(line) for line in text.splitlines() if line.startswith('PT_SHADER_FOG_PROFILE ')]
        rows=rows[3:-3]
        assert len(rows)>=8
        for row in rows:
            assert all(math.isfinite(v) and v>=0 for v in row.values()) and row['pixels']>0
            assert row['scattered']<=row['events']<=row['bounded_segments']
            assert sum(row[k] for k in ('sampling','lightsetup','visibility','transmittance'))<=100.01
        assert sum(row['events'] for row in rows)>0 and sum(row['shadows'] for row in rows)>0
        print('Fog diagnostic medians (exclusive clock shares, NOT production GPU ms):',
              {k:round(statistics.median(row[k] for row in rows),3) for k in rows[0]})
    if args.log:
        baseline, shares, diagnostic = parse(args.log.read_text(errors="replace"))
        for name, rows in baseline.items():
            print(f"{name}: {len(rows)} settled frames; engine {statistics.median(r['frame'] for r in rows):.3f} ms; "
                  f"GPU tracing {statistics.median(r['trace'] for r in rows):.3f} ms")
        weight = sum(row["ticks"] for row in shares)
        print(f"Instrumented exclusive clock shares ({len(shares)} frames; estimates, NOT GPU ms):")
        for key in ("queries", "materials", "lighting", "other"):
            print(f"  {key}: {sum(r[key] * r['ticks'] for r in shares) / weight:.2f}%")
        if all(key in row for row in shares for key in LIGHT_PARTS):
            for key in LIGHT_PARTS:
                print(f"    lighting/{key}: {sum(r[key] * r['ticks'] for r in shares) / weight:.2f}% of total")
        paths = sum(row["paths"] for row in shares)
        for key in ("rays", "shadows", "hits"):
            print(f"  {key} per sampled path: {sum(r[key] for r in shares) / paths:.3f}")
        print(f"Instrumented GPU tracing median: {statistics.median(r['trace'] for r in diagnostic):.3f} ms; "
              "not a production performance result")
    else:
        unittest.main(argv=[__file__])
