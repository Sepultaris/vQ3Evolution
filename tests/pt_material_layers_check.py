"""Run actual native material conversion and GLSL composition without a GPU.

Production function bodies are compiled unchanged (GLSL 'out' is adapted to a
C++ reference). CPU texture samples are mocked: these are regression tests, not
proof of in-game visual quality. No game data, settings, or GPU are modified.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
RENDERER = ROOT / "code/renderer_vulkan"


def run(cc, cxx, sdk):
    source = (RENDERER / "vk_pathtrace.c").read_text()
    types = source[source.index("typedef struct { float a[4]"):source.index("typedef struct { uint32_t primitive;")]
    functions = source[source.index("static void copy_wave("):source.index("static void load_map_lights(")]
    shader = (RENDERER / "shaders/pt_material_layers.glsl").read_text()
    integrator = (RENDERER / "shaders/pt_integrator.glsl").read_text()
    emission = integrator[integrator.index("vec3 emissionAt("):integrator.index("vec3 visibility(")]
    alpha = integrator[integrator.index("bool passesAlpha("):integrator.index('#include "pt_glow_coverage.glsl"')]
    alpha += (RENDERER / "shaders/pt_glow_coverage.glsl").read_text()
    shell = (RENDERER / "shaders/pt_reflective_shell.glsl").read_text().split("vec4 reflectiveShellAppearance(")[0]
    vertex_type = source[source.index("typedef struct { float normal["):source.index("typedef struct {\n    uint32_t key[")]
    vertex_upload = source[source.index("void vk_pt_world_vertex("):source.index("void vk_pt_world_surface(")]
    with tempfile.TemporaryDirectory(prefix="pt-material-layers-") as folder:
        folder = Path(folder)
        (folder / "pt_material_types.inc").write_text(types)
        (folder / "pt_material_code.inc").write_text(functions)
        (folder / "pt_material_layers.inc").write_text(shader.replace("out vec4 ", "vec4 &"))
        (folder / "pt_material_emission.inc").write_text(emission.replace(".rgb", ".rgb()"))
        (folder / "pt_glow_coverage.inc").write_text(alpha)
        (folder / "pt_reflective_shell.inc").write_text(shell.replace(".rgb", ".rgb()").replace("out vec3 ", "vec3 &"))
        (folder / "pt_world_vertex_type.inc").write_text(vertex_type)
        (folder / "pt_world_vertex_upload.inc").write_text(vertex_upload)
        for compiler, fixture, name, includes in (
            (cc, "pt_material_layers_fixture.c", "conversion", [RENDERER, sdk / "Include"]),
            (cxx, "pt_material_blend_fixture.cpp", "composition", []),
        ):
            executable = folder / (name + (".exe" if os.name == "nt" else ""))
            command = [compiler, str(ROOT / "tests" / fixture), "-O1", "-I", str(folder)]
            for include in includes:
                command += ["-I", str(include)]
            subprocess.run(command + ["-o", str(executable)], check=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=10)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=shutil.which("gcc") or "gcc")
    parser.add_argument("--cxx", default=shutil.which("g++") or "g++")
    parser.add_argument("--sdk", type=Path, default=Path(os.environ.get("VULKAN_SDK", "/usr")))
    args = parser.parse_args()
    run(args.cc, args.cxx, args.sdk)
