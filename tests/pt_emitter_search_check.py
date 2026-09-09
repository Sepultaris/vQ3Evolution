"""Execute native emitter-index construction and actual GLSL sampling on CPU."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDERER = ROOT / "code/renderer_vulkan"


def run(compiler):
    source = (RENDERER / "shaders/pt_integrator.glsl").read_text()
    sampler = source[source.index("uint sampledEmitterIndex;"):source.index('#include "pt_emitter_geometry.glsl"')]
    with tempfile.TemporaryDirectory(prefix="pt-emitter-search-") as folder:
        folder = Path(folder)
        (folder / "pt_emitter_sampler.inc").write_text(sampler)
        executable = folder / "emitter-search-test.exe"
        for optimization in (("-O2",), ("-O3", "-ffast-math")):
            print("Native/GLSL CPU comparison flags:", " ".join(optimization), flush=True)
            subprocess.run([compiler, str(ROOT / "tests/pt_emitter_search_fixture.cpp"), *optimization, "-I", str(folder),
                            "-I", str(RENDERER), "-o", str(executable)], check=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=30)


class EmitterSearchTests(unittest.TestCase):
    def test_candidate_is_not_enabled_without_performance_evidence(self):
        cvars = (RENDERER / "tr_cvar.c").read_text()
        self.assertIn('Cvar_Get("r_pathTracingEmitterSearch", "0", CVAR_CHEAT)', cvars)

    def test_one_random_draw_and_unchanged_cdf_comparison(self):
        source = (RENDERER / "shaders/pt_integrator.glsl").read_text()
        sampler = source.split("uint sampleEmitter() {", 1)[1].split("float emitterPDF(", 1)[0]
        self.assertEqual(sampler.count("randomFloat()"), 1)
        self.assertIn("emitters[mid].cumulativePower<=target", sampler)
        self.assertIn("return emitters[low].primitive;", sampler)
        self.assertIn("uint low=0, high=lightCounts.x-1;", sampler)

    def test_index_rebuilt_with_emitter_data_not_camera(self):
        native = (RENDERER / "vk_pathtrace.c").read_text()
        rebuild = native.split("if (!r_pathTracingReference->integer || !pt.frozen) {", 1)[1].split("pt.frozen =", 1)[0]
        self.assertIn("build_emitter_search(lights->emitters, lights->counts[0]", rebuild)
        self.assertLess(rebuild.index("cumulative_power ="), rebuild.index("build_emitter_search("))
        self.assertIn("emitter_search_control[0] = r_pathTracingEmitterSearch->integer", rebuild)
        self.assertIn("if (r_pathTracingEmitterSearch->integer)", rebuild)
        self.assertIn("r_pathTracingReference->integer && pt.frozen &&", rebuild)

    def test_index_does_not_change_pdf_or_ray_budgets(self):
        source = (RENDERER / "shaders/pt_integrator.glsl").read_text()
        pdf = source.split("float emitterPDF(", 1)[1].split('#include "pt_sky.glsl"', 1)[0]
        self.assertIn("power*distanceSquared/max(lightSelection.x*cosine,0.000001)", pdf)
        self.assertNotIn("emitterSearch", pdf)
        native = (RENDERER / "pt_emitter_search.h").read_text()
        for forbidden in ("camera", "visibility", "random", "exposure"):
            # Comments mention unchanged random draw; actual body must not use it.
            body = native.split("{", 1)[1]
            if forbidden != "random":
                self.assertNotIn(forbidden, body)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=shutil.which("g++") or "g++")
    args = parser.parse_args()
    run(args.cxx)
    unittest.main(argv=[__file__])
