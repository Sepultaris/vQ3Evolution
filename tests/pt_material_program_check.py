"""Check UV-only input contracts and summarize a completed same-process A/B/B/A run."""
import argparse
from pathlib import Path
import re
import statistics
import unittest

ROOT = Path(__file__).resolve().parents[1]
PHASES = ("REFERENCE_A", "OPTIMIZED_A", "OPTIMIZED_B", "REFERENCE_B")


def parse(text, minimum=40, phases=PHASES):
    phase = None
    seen = []
    rows = {name: [] for name in phases}
    complete = False
    for line in text.splitlines():
        if line == "PT_PROGRAM_COMPLETE":
            complete = True
            phase = None
        elif line.startswith("PT_PROGRAM_"):
            phase = line.removeprefix("PT_PROGRAM_")
            if phase not in rows or phase in seen:
                raise ValueError("Unknown or repeated A/B phase")
            seen.append(phase)
        elif line.startswith("PT_PROFILE ") and phase:
            values = {key: float(value) for key, value in re.findall(r"(\w+)=([\d.]+)", line)}
            if set(values) != {"frame", "raster", "as", "trace", "guides", "temporal", "spatial", "post"}:
                raise ValueError("Incomplete GPU timing row")
            rows[phase].append(values)
    if not complete or tuple(seen) != phases or "Path tracer: active 1" not in text:
        raise ValueError("Run incomplete: do not claim a performance result")
    for name in phases:
        # Discard transition query readback at each edge, even after warm-up.
        rows[name] = rows[name][3:-3]
        if len(rows[name]) < minimum:
            raise ValueError(f"Too few settled samples in {name}")
    return rows


class ProgramTests(unittest.TestCase):
    def test_shared_program_keeps_time_and_gradients(self):
        source = (ROOT / "code/renderer_vulkan/shaders/pt_integrator.glsl").read_text()
        sampler = source.split("vec4 layerSample(uint primitive", 1)[1].split("vec4 layerSample(uint primitive", 1)[0]
        self.assertIn("context.timeScroll=vertices[t.x].meta.xyz;", sampler)
        minimal = sampler.split("if(program==2) {", 1)[1].split("} else {", 1)[0]
        for expensive in ("position(", "vertices[", "environmentMaterial", "textureGradients("):
            self.assertNotIn(expensive, minimal)
        self.assertIn("return layerSampleUV(id,layerIndex,uv,gradients,context);", sampler)
        self.assertIn("if(program==1)", sampler)
        program = source.split("vec4 layerSampleUV(", 1)[1].split('#include "pt_environment_material', 1)[0]
        self.assertIn("vectors[0].w==1", program)
        # Inputs removed by native classification are guarded by these modes.
        for condition in ("if(layer.params.z==6)", "if(type==2)", "if(rgb==3)", "if(rgb==4)",
                          "(rgb==5 || rgb==6)", "rgb==7", "if(alpha==2)", "if(alpha==3)", "if(alpha==4)", "if(alpha==5)"):
            self.assertIn(condition, program)

    def test_toggle_only_changes_registered_program_markers(self):
        native = (ROOT / "code/renderer_vulkan/vk_pathtrace.c").read_text()
        toggle = native.split("if (pt.materials_dirty || pt.material_program_mode", 1)[1].split("if (!r_pathTracingReference", 1)[0]
        self.assertIn("if (pt.material_shaders[material])", toggle)
        self.assertIn("*program == 2 || *program == -2", toggle)
        self.assertIn("pt.materials_dirty = qtrue;", toggle)
        self.assertNotIn("reset_history =", toggle)
        self.assertNotIn("pt.history =", toggle)

    def test_benchmark_defaults_to_saved_graphics(self):
        runner = (ROOT / "tests/run-pt-performance.ps1").read_text()
        self.assertIn("if (!$Synthetic)", runner)
        self.assertIn("default benchmarks match saved settings", runner)
        self.assertIn("com_maxfps[A-Za-z]*", runner)
        whitelist = re.search(r"r_pathTracing\(([^)]+)\) ", runner).group(1).split('|')
        self.assertTrue({'Profile','ShaderProfile','MaterialFastPath','TestScene','TestMotion'} <= set(whitelist))
        self.assertFalse({'Samples','Bounces','Exposure','Adaptive'} & set(whitelist))
        self.assertIn("sourceSha256", runner)
        self.assertIn("produced no fresh log", runner)
        # Resolution/DLSS overrides must be confined to explicit synthetic mode.
        arguments = runner.split('$ptArguments = ', 1)[1].split('$ptWindowStyle', 1)[0]
        synthetic = arguments.split('if ($Synthetic) {', 1)[1].split('}', 1)[0]
        for override in ("r_fullscreen 0", "r_customwidth $Width", "r_dlss $Dlss", "r_dlssFrameGeneration 0", "r_swapInterval 0"):
            self.assertIn(override, synthetic)

    @staticmethod
    def fixture():
        row = "PT_PROFILE frame=25 raster=0.1 as=0.4 trace=17 guides=0.5 temporal=0.4 spatial=1 post=4\n"
        return "".join("PT_PROGRAM_" + name + "\n" + row * 55 for name in PHASES) + \
               "PT_PROGRAM_COMPLETE\nPath tracer: active 1\n"

    def test_complete_abba(self):
        rows = parse(self.fixture())
        self.assertEqual(tuple(rows), PHASES)
        self.assertEqual(len(rows["REFERENCE_A"]), 49)

    def test_reject_partial_or_stale_log(self):
        for text in ("", self.fixture().replace("PT_PROGRAM_COMPLETE", "timed out"),
                     self.fixture().replace("Path tracer: active 1", ""),
                     self.fixture().replace("PT_PROGRAM_OPTIMIZED_A", "PT_PROGRAM_REFERENCE_A")):
            with self.assertRaises(ValueError):
                parse(text)

    def test_reject_missing_timings(self):
        with self.assertRaises(ValueError):
            parse(self.fixture().replace(" guides=0.5", ""))

    def test_explicit_reversed_comparison_keeps_acceptance_requirements(self):
        phases = ("OPTIMIZED_A", "REFERENCE_A", "REFERENCE_B", "OPTIMIZED_B")
        row = "PT_PROFILE frame=25 raster=0.1 as=0.4 trace=17 guides=0.5 temporal=0.4 spatial=1 post=4\n"
        text = "".join("PT_PROGRAM_" + name + "\n" + row * 55 for name in phases) + \
               "PT_PROGRAM_COMPLETE\nPath tracer: active 1\n"
        self.assertEqual(tuple(parse(text, phases=phases)), phases)
        for bad in (text.replace("PT_PROGRAM_COMPLETE", "timeout"), text.replace("PT_PROGRAM_REFERENCE_B", "PT_PROGRAM_REFERENCE_A")):
            with self.assertRaises(ValueError):
                parse(bad, phases=phases)
        with self.assertRaises(ValueError):
            parse(text)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", nargs="?", type=Path)
    parser.add_argument("--reverse", action="store_true", help="Expect candidate/reference/reference/candidate phases")
    args = parser.parse_args()
    if args.log:
        text = args.log.read_text(errors="replace")
        rows = parse(text, phases=("OPTIMIZED_A", "REFERENCE_A", "REFERENCE_B", "OPTIMIZED_B") if args.reverse else PHASES)
        views = re.findall(r"\((-?\d+) (-?\d+) (-?\d+)\) : (-?\d+)", text)
        if not views or any(abs(int(value)-target)>8 for value, target in zip(views[0], (-1510, 200, 50, 0))):
            raise ValueError("Initial benchmark camera was not verified")
        for name, values in rows.items():
            frames = sorted(row["frame"] for row in values)
            trace = statistics.median(row["trace"] for row in values)
            print(f"{name}: {len(values)} frames, median {statistics.median(frames):.3f} ms, "
                  f"p95 {frames[int(.95*(len(frames)-1))]:.3f} ms, tracing {trace:.3f} ms")
        a = statistics.median(row["trace"] for name in PHASES if name.startswith("REFERENCE") for row in rows[name])
        b = statistics.median(row["trace"] for name in PHASES if name.startswith("OPTIMIZED") for row in rows[name])
        print(f"Tracing median change: {(b/a-1)*100:+.2f}% (negative is faster; one short run is not broad validation)")
    else:
        unittest.main(argv=[__file__])
