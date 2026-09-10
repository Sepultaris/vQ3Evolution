"""Actual native alias tables and GLSL selection: same light/PDF with one read."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDERER = ROOT / "code/renderer_vulkan"


def run(compiler):
    native = (RENDERER / "vk_pathtrace.c").read_text()
    builder = native[native.index("static void build_light_grid("):native.index("void vk_pt_begin_world(")]
    shader = (RENDERER / "shaders/pt_sampling.glsl").read_text().split("uint proposedLight(", 1)[1]
    shader = "uint proposedLight(" + shader.replace("out float pdf", "float &pdf")
    with tempfile.TemporaryDirectory(prefix="pt-alias-pdf-") as folder:
        folder = Path(folder)
        (folder / "pt_alias_builder.inc").write_text(builder)
        (folder / "pt_alias_sampler.inc").write_text(shader)
        executable = folder / "alias-pdf.exe"
        for flags in (("-O2",), ("-O3", "-ffast-math")):
            print("Native table / GLSL sampler:", " ".join(flags), flush=True)
            subprocess.run([compiler, *flags, str(ROOT / "tests/pt_alias_pdf_fixture.cpp"), "-I", str(folder),
                            "-I", str(RENDERER), "-o", str(executable)], check=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=30)


class AliasPDFTests(unittest.TestCase):
    def test_candidate_stays_off_until_measured(self):
        source = (RENDERER / "tr_cvar.c").read_text()
        self.assertIn('Cvar_Get("r_pathTracingAliasPDF", "0", CVAR_CHEAT)', source)

    def test_probabilities_are_copied_after_alias_construction(self):
        helper = (RENDERER / "pt_alias_pdf.h").read_text()
        self.assertIn("entries[i][3] = entries[(uint32_t)entries[i][1]][2]", helper)
        native = (RENDERER / "vk_pathtrace.c").read_text()
        builder = native.split("static void build_light_grid(", 1)[1].split("void vk_pt_begin_world(", 1)[0]
        self.assertLess(builder.index("while (nl)"), builder.index("cache_alias_probabilities("))
        self.assertIn("0.2/n+0.8*weights[i]/sum", builder)

    def test_specialization_and_diagnostics_match(self):
        shader = (RENDERER / "shaders/pt_sampling.glsl").read_text()
        self.assertIn("layout(constant_id=1) const bool ptAliasPDF=false", shader)
        self.assertIn("pdf=ptAliasPDF ? (direct ? entry.z:entry.w):lightAliases[offset+chosen].z", shader)
        native = (RENDERER / "vk_pathtrace.c").read_text()
        self.assertIn("{ 1, sizeof(VkBool32), sizeof(VkBool32) }", native)
        self.assertIn("{ 4, entries, sizeof(options), options }", native)
        profile = (RENDERER / "pt_shader_profile.h").read_text()
        self.assertIn("{ 1, sizeof(VkBool32), sizeof(VkBool32) }", profile)
        self.assertIn("{ 3, entries, sizeof(options), options }", profile)

    def test_same_storage_and_no_runtime_grid_rebuild(self):
        native = (RENDERER / "vk_pathtrace.c").read_text()
        self.assertIn("float entries[PT_LIGHT_CELLS * PT_MAX_MAP_LIGHTS][4];", native)
        record = native.split("qboolean vk_pt_record(", 1)[1].split("void vk_pt_info_f(", 1)[0]
        self.assertNotIn("cache_alias_probabilities", record)
        self.assertNotIn("build_light_grid()", record)
        self.assertIn("if (requested & 4) return lighting_pipeline_select(requested & 3);", native)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=shutil.which("g++") or "g++")
    args = parser.parse_args()
    run(args.cxx)
    unittest.main(argv=[__file__])
