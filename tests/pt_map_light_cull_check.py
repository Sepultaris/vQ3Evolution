"""Exercise production GLSL map-light reservoir math with early rejection on/off."""
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
    begin = source.index("float pointAttenuation(")
    attenuation = source[begin:source.index("\n}\n",begin)+3]
    reservoir = source[source.index("float totalWeight=0, selectedWeight=0;"):source.index("if(selectedWeight>0)")]
    sampling = (RENDERER / "shaders/pt_sampling.glsl").read_text().split("uint proposedLight(", 1)[1]
    code = attenuation.replace("pointAttenuation(", "actualPointAttenuation(")
    code += "\nfloat pointAttenuation(uint i, vec3 l, float d) { ++attenuationCalls; return actualPointAttenuation(i,l,d); }\n"
    code += "uint proposedLight(" + sampling.replace("out float pdf", "float &pdf")
    code += "\nResult reservoir(vec3 start, vec3 n, vec3 geometric) {\n" + reservoir
    code += "\nreturn {selected, candidateCount, totalWeight, selectedWeight};\n}\n"
    code = code.replace(".xyz", ".xyz()").replace(".rgb", ".xyz()")
    with tempfile.TemporaryDirectory(prefix="pt-map-light-") as folder:
        folder = Path(folder)
        (folder / "pt_map_light_math.inc").write_text(code)
        executable = folder / "map-light-test.exe"
        for flags in (("-O2",), ("-O3", "-ffast-math")):
            print("GLSL CPU comparison flags:", " ".join(flags), flush=True)
            subprocess.run([compiler, str(ROOT / "tests/pt_map_light_cull_fixture.cpp"), *flags,
                            "-I", str(folder), "-o", str(executable)], check=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=30)


def validate_specializations(sdk):
    tools = Path(sdk) / "Bin"
    with tempfile.TemporaryDirectory(prefix="pt-map-light-spv-") as folder:
        for shader in ("pathtrace", "pt_brdf", "pt_profile", "pt_profile_brdf"):
            source = RENDERER / "shaders/Compiled" / (shader + ".cspv")
            assembly = subprocess.check_output([str(tools / "spirv-dis.exe"), str(source)], text=True)
            assert "OpDecorate %ptMapLightCull SpecId 0" in assembly
            assert "%ptMapLightCull = OpSpecConstantFalse %bool" in assembly
            outputs = []
            for value in ("false", "true"):
                output = Path(folder) / (shader + "-" + value + ".spv")
                subprocess.run([str(tools / "spirv-opt.exe"), "--set-spec-const-default-value", "0:" + value,
                                "--freeze-spec-const", "--fold-spec-const-op-composite", "-O",
                                str(source), "-o", str(output)], check=True, timeout=30)
                subprocess.run([str(tools / "spirv-val.exe"), "--target-env", "vulkan1.2", str(output)],
                               check=True, timeout=30)
                frozen = subprocess.check_output([str(tools / "spirv-dis.exe"), str(output)], text=True)
                assert "OpSpecConstant" not in frozen and "SpecId" not in frozen
                outputs.append(output.read_bytes())
            assert outputs[0] != outputs[1], "Specialization must actually change generated shader code"
            print(shader + ": both frozen specializations optimized/validated", flush=True)


class MapLightCullTests(unittest.TestCase):
    def test_candidate_is_not_a_saved_quality_setting(self):
        source = (RENDERER / "tr_cvar.c").read_text()
        self.assertIn('Cvar_Get("r_pathTracingMapLightCull", "0", CVAR_CHEAT)', source)

    def test_identical_rejection_and_unchanged_candidate_budget(self):
        source = (RENDERER / "shaders/pt_integrator.glsl").read_text()
        loop = source.split("float totalWeight=0, selectedWeight=0;", 1)[1].split("if(selectedWeight>0)", 1)[0]
        self.assertIn("candidateCount=min(lightCounts.w,8u)", loop)
        self.assertEqual(loop.count("dot(geometric,l)<=0"), 2)
        self.assertEqual(loop.count("randomFloat()"), 2)
        self.assertLess(loop.index("if(skyEnvironment.w<=0 && ptMapLightCull"), loop.index("float weight="))
        self.assertIn("(skyEnvironment.w<=0 && !ptMapLightCull && dot(geometric,l)<=0) || weight<=0", loop)
        self.assertIn("if(skyEnvironment.w>0) weight=",loop)

    def test_specialization_matches_diagnostics_without_new_buffers(self):
        shader = (RENDERER / "shaders/pt_integrator.glsl").read_text()
        self.assertIn("layout(constant_id=0) const bool ptMapLightCull=false;", shader)
        native = (RENDERER / "vk_pathtrace.c").read_text()
        self.assertIn("VkSpecializationMapEntry entries[4] = { { 0, 0, sizeof(VkBool32) },", native)
        self.assertIn("pSpecializationInfo = &specialization", native)
        profile = (RENDERER / "pt_shader_profile.h").read_text()
        self.assertIn("VkSpecializationMapEntry entries[3] = { { 0, 0, sizeof(VkBool32) },", profile)
        self.assertIn("pSpecializationInfo = &specialization", profile)
        self.assertIn("pt_shader_profile.map_light_cull == map_light_cull", profile)

    def test_independent_pipeline_modes_keep_proven_reuse_on_failure(self):
        native = (RENDERER / "vk_pathtrace.c").read_text()
        self.assertIn("r_pathTracingMapLightCull->integer ? 2u : 0u", native)
        self.assertIn("requested == 3 && lighting_pipeline_initialize(1)", native)
        self.assertIn("if (pt.lighting_pipelines[mode]) qvkDestroyPipeline", native)
        self.assertIn("PT_MAP_LIGHT_MODE enabled=%d requested=%d", native)

    def test_cold_compilation_timing_is_separate_from_gpu_samples(self):
        native = (RENDERER / "vk_pathtrace.c").read_text()
        initialize = native.split("static qboolean lighting_pipeline_initialize(", 1)[1].split("void vk_pt_profile(", 1)[0]
        self.assertLess(initialize.index("if (pt.lighting_pipelines[mode]) return qtrue"),
                        initialize.index("PT_LIGHTING_PIPELINE_BEGIN"))
        self.assertIn("PT_LIGHTING_PIPELINE_END mode=%u cpu_ms=%d result=%d", initialize)
        self.assertNotIn("qvkDeviceWaitIdle", initialize)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=shutil.which("g++") or "g++")
    parser.add_argument("--sdk", help="Also optimize and validate both compiled Vulkan specializations")
    args = parser.parse_args()
    run(args.cxx)
    if args.sdk:
        validate_specializations(args.sdk)
    unittest.main(argv=[__file__])
