"""Run original/cached GLSL BRDF math on CPU and guard native pipeline isolation."""
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
    original = source[source.index("vec3 basisSample("):source.index("float powerWeight(")]
    cached = (RENDERER / "shaders/pt_brdf_reuse.glsl").read_text()
    with tempfile.TemporaryDirectory(prefix="pt-brdf-reuse-") as folder:
        folder = Path(folder)
        for filename, code in (("pt_original_brdf.inc", original), ("pt_brdf_reuse.inc", cached)):
            (folder / filename).write_text(code.replace("out float ", "float &"))
        executable = folder / "brdf-reuse-test.exe"
        for flags in (("-O2",), ("-O3", "-ffast-math")):
            print("GLSL CPU comparison flags:", " ".join(flags), flush=True)
            subprocess.run([compiler, str(ROOT / "tests/pt_brdf_reuse_fixture.cpp"), *flags, "-I", str(folder),
                            "-o", str(executable)], check=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=30)


class BRDFReuseTests(unittest.TestCase):
    def test_validated_reuse_defaults_on_without_archiving_a_user_setting(self):
        cvars = (RENDERER / "tr_cvar.c").read_text()
        self.assertIn('Cvar_Get("r_pathTracingBRDFReuse", "1", CVAR_CHEAT)', cvars)

    def test_all_five_evaluations_and_continuation_share_current_hit(self):
        source = (RENDERER / "shaders/pt_integrator.glsl").read_text()
        hit = source.split("void integrator(", 1)[1].split("vec3 toneMap(", 1)[0]
        self.assertEqual(hit.count("prepareHitBRDF("), 1)
        self.assertEqual(hit.count("evaluateHitBRDF("), 5)
        self.assertEqual(hit.count("sampleHitBRDF("), 1)
        self.assertLess(hit.index("vec2 properties=surfaceProperties("), hit.index("prepareHitBRDF("))
        self.assertLess(hit.index("prepareHitBRDF("), hit.index("uint primitive=sampleEmitter();"))

    def test_no_temporal_or_material_storage_for_reuse(self):
        shader = (RENDERER / "shaders/pt_brdf_reuse.glsl").read_text()
        self.assertIn("vec4 brdfView=prepareBRDFView", shader)
        for external in ("layout(", "buffer ", "previousPosition", "history[", "materials["):
            self.assertNotIn(external, shader)

    def test_native_optional_pipeline_lifecycle_and_profiling_match(self):
        native = (RENDERER / "vk_pathtrace.c").read_text()
        self.assertIn("r_pathTracingBRDFReuse->integer ? 1u : 0u", native)
        self.assertIn("pt.lighting_pipelines[lighting_mode]);", native)
        self.assertIn("if (pt.lighting_pipelines[mode]) qvkDestroyPipeline", native)
        self.assertIn("requested == 3 && lighting_pipeline_initialize(1)", native)
        self.assertIn("shader_profile_bind(cmd, brdf_active, map_light_cull_active, alias_pdf_active, emitter_geometry_active, light_loop_active)", native)
        profile = (RENDERER / "pt_shader_profile.h").read_text()
        self.assertIn("pt_shader_profile.brdf_reuse == brdf_reuse", profile)
        self.assertIn("brdf_reuse ? pt_profile_brdf_comp_spv : pt_profile_comp_spv", profile)

    def test_original_shader_is_separate_from_candidate(self):
        original = (RENDERER / "shaders/pathtrace.comp").read_text()
        candidate = (RENDERER / "shaders/pt_brdf.comp").read_text()
        self.assertNotIn("PT_BRDF_REUSE", original)
        self.assertIn("#define PT_BRDF_REUSE 1", candidate)
        helper = (RENDERER / "shaders/pt_brdf_reuse.glsl").read_text()
        self.assertIn("#ifdef PT_BRDF_REUSE", helper)
        self.assertIn("evaluateBRDF(n,v,l,albedo,roughness,metallic,pdf)", helper)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=shutil.which("g++") or "g++")
    args = parser.parse_args()
    run(args.cxx)
    unittest.main(argv=[__file__])
