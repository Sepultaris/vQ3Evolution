"""Compile actual native PT pipeline selection against a GPU-free Vulkan mock."""
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
    functions = native[native.index("static qboolean lighting_pipeline_initialize("):
                       native.index("void vk_pt_profile(")]
    with tempfile.TemporaryDirectory(prefix="pt-pipeline-lifecycle-") as folder:
        folder = Path(folder)
        (folder / "pt_pipeline_functions.inc").write_text(functions)
        executable = folder / "pipeline-lifecycle.exe"
        subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "tests/pt_pipeline_lifecycle_fixture.c"), "-I", str(folder),
                        "-I", str(RENDERER), "-o", str(executable)], check=True, timeout=30)
        subprocess.run([str(executable)], check=True, timeout=30)


class PipelineLifecycleTests(unittest.TestCase):
    def test_startup_builds_selected_integrator_only(self):
        native = (RENDERER / "vk_pathtrace.c").read_text()
        startup = native.split("qboolean vk_pt_initialize(", 1)[1].split("void vk_pt_shutdown(", 1)[0]
        self.assertIn("if (!lighting_pipeline_select(lighting_pipeline_requested())) goto fail;", startup)
        self.assertNotIn("pathtrace_comp_spv", startup)
        self.assertEqual(startup.count("qvkCreateComputePipelines("), 3)  # Guides + two reconstruction passes.
        self.assertLess(startup.index("qvkCreatePipelineLayout("), startup.index("lighting_pipeline_select("))

    def test_record_resolves_a_valid_pipeline_before_dispatch(self):
        native = (RENDERER / "vk_pathtrace.c").read_text()
        record = native.split("qboolean vk_pt_record(", 1)[1].split("void vk_pt_info_f(", 1)[0]
        self.assertLess(record.index("if (!lighting_pipeline_select("), record.index("qvkCmdDispatch("))
        self.assertIn("uint32_t lighting_mode = pt.lighting_mode;", record)
        self.assertIn("pt.lighting_pipelines[lighting_mode]);", record)
        self.assertNotIn("pt.pipeline", native)

    def test_shutdown_releases_all_modes_before_layout(self):
        native = (RENDERER / "vk_pathtrace.c").read_text()
        shutdown = native.split("void vk_pt_shutdown(", 1)[1].split("static uint32_t hash_bytes(", 1)[0]
        self.assertLess(shutdown.index("lighting_pipeline_shutdown();"), shutdown.index("qvkDestroyPipelineLayout("))

    def test_quality_defaults_and_shader_payloads_are_not_changed(self):
        cvars = (RENDERER / "tr_cvar.c").read_text()
        self.assertIn('Cvar_Get("r_pathTracingBRDFReuse", "1", CVAR_CHEAT)', cvars)
        self.assertIn('Cvar_Get("r_pathTracingMapLightCull", "0", CVAR_CHEAT)', cvars)
        native = (RENDERER / "vk_pathtrace.c").read_text()
        lifecycle = native.split("static qboolean lighting_pipeline_initialize(", 1)[1].split("void vk_pt_profile(", 1)[0]
        for side_effect in ("Cvar_Set", "buffer_create", "reset_history", "qvkDeviceWaitIdle", "qvkQueueWaitIdle"):
            self.assertNotIn(side_effect, lifecycle)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=shutil.which("gcc") or "gcc")
    args = parser.parse_args()
    run(args.cc)
    unittest.main(argv=[__file__])
