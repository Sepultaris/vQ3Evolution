"""Verify packed emissive geometry without changing samples or shading math."""
import argparse
from pathlib import Path
import shutil
import re
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
RENDERER=ROOT / "code/renderer_vulkan"


def run(compiler):
    source=(RENDERER / "shaders/pt_emitter_geometry.glsl").read_text()
    source=source.replace("layout(constant_id=2) const bool ptEmitterGeometry=false;", "")
    source=source.replace("out vec3 ", "vec3 &").replace("out float ", "float &")
    with tempfile.TemporaryDirectory(prefix="pt-emitter-geometry-") as folder:
        folder=Path(folder)
        (folder / "pt_emitter_geometry.inc").write_text(source)
        executable=folder / "geometry.exe"
        for flags in (("-O2",), ("-O3", "-ffast-math")):
            print("Packing / GLSL equivalence:", " ".join(flags), flush=True)
            subprocess.run([compiler, *flags, str(ROOT / "tests/pt_emitter_geometry_fixture.cpp"),
                            "-I", str(folder), "-I", str(RENDERER), "-o", str(executable)], check=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=30)


class EmitterGeometryTests(unittest.TestCase):
    def test_repeatedly_measured_default_keeps_original_available(self):
        self.assertIn('Cvar_Get("r_pathTracingEmitterGeometry", "1", CVAR_CHEAT)', (RENDERER / "tr_cvar.c").read_text())

    def test_cache_is_current_geometry_not_view_dependent_or_quantized(self):
        native=(RENDERER / "vk_pathtrace.c").read_text()
        record=native.split("qboolean vk_pt_record(", 1)[1].split("void vk_pt_info_f(", 1)[0]
        self.assertIn("cache_emitter_geometry(&lights->emitter_geometry[lights->counts[0]], a, b, c,", record)
        self.assertLess(record.index("cache_emitter_geometry("), record.index("lights->counts[0]++"))
        helper=(RENDERER / "pt_emitter_geometry.h").read_text()
        self.assertEqual(helper.count("memcpy("),3)
        self.assertNotIn("sqrt",helper)
        self.assertNotIn("normal",helper.split("typedef struct",1)[1])

    def test_random_draws_and_light_selection_unchanged(self):
        shader=(RENDERER / "shaders/pt_integrator.glsl").read_text()
        sampling=shader.split("uint sampleEmitter()",1)[1].split('#include "pt_emitter_geometry.glsl"',1)[0]
        self.assertEqual(sampling.count("randomFloat()"),1)
        self.assertIn("emitters[mid].cumulativePower<=target",sampling)
        self.assertIn("sampledEmitterIndex=low;\n    return emitters[low].primitive;",sampling)
        helper=(RENDERER / "shaders/pt_emitter_geometry.glsl").read_text()
        for change in ("randomFloat", "texture", "normalize", "clamp"):
            self.assertNotIn(change,helper)
        self.assertIn("emitterPower*(distance*distance)/max(lightSelection.x*lc,0.000001)",shader)

    def test_existing_light_layout_prefix_preserved(self):
        native=(RENDERER / "vk_pathtrace.c").read_text()
        self.assertIn("uint32_t emitter_search_ranges[PT_EMITTER_SEARCH_BUCKETS][2]; // std430 uvec4[32].\n    pt_emitter_geometry_t emitter_geometry[PT_MAX_EMITTERS];",native)
        shader=(RENDERER / "shaders/pt_integrator.glsl").read_text()
        self.assertIn("uvec4 emitterSearchRanges[32]; // Two inclusive [low,high] pairs per vector.\n    EmitterGeometry emitterGeometry[8192];",shader)

    def test_optional_variant_is_compiled_not_a_dynamic_pixel_branch(self):
        helper=(RENDERER / "shaders/pt_emitter_geometry.glsl").read_text()
        self.assertIn("layout(constant_id=2) const bool ptEmitterGeometry=false;",helper)
        self.assertNotIn("emitterSearchControl.z",helper)
        for filename in ("vk_pathtrace.c","pt_shader_profile.h"):
            native=(RENDERER / filename).read_text()
            self.assertIn("{ 2, 2*sizeof(VkBool32), sizeof(VkBool32) }",native)


def check_specialization(sdk):
    sdk=Path(sdk)/"Bin"
    with tempfile.TemporaryDirectory(prefix="pt-emitter-specialization-") as folder:
        for name in ("pathtrace","pt_brdf","pt_profile","pt_profile_brdf"):
            for enabled in (False,True):
                output=Path(folder)/"frozen.spv"
                subprocess.run([str(sdk/"spirv-opt.exe"),"--set-spec-const-default-value",
                                f"0:false 1:false 2:{str(enabled).lower()}","--freeze-spec-const",
                                "--fold-spec-const-op-composite","-O",str(RENDERER/f"shaders/Compiled/{name}.cspv"),
                                "-o",str(output)],check=True,timeout=30)
                subprocess.run([str(sdk/"spirv-val.exe"),"--target-env","vulkan1.2",str(output)],check=True,timeout=30)
                code=subprocess.check_output([str(sdk/"spirv-dis.exe"),str(output)],text=True)
                # Find the descriptor variable by binding, independent of generated names.
                binding=re.search(r'OpDecorate\s+(%\w+)\s+Binding\s+10\b',code)[1]
                indices=re.findall(r'(%\w+)\s*=\s*OpConstant\s+%\w+\s+13\b',code)
                reads=[]
                for index in indices:
                    reads.extend(re.findall(r'OpAccessChain\s+%\w+\s+'+re.escape(binding)+r'\s+'+re.escape(index)+r'\s',code))
                assert bool(reads)==enabled,(name,enabled,len(reads))
                print(f"PASS {name} packed={enabled}: {len(reads)} packed-geometry access chains",flush=True)


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx",default=shutil.which("g++") or "g++")
    parser.add_argument("--sdk")
    args=parser.parse_args()
    run(args.cxx)
    if args.sdk:
        check_specialization(args.sdk)
    unittest.main(argv=[__file__])
