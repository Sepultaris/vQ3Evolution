"""CPU-only tests of the actual sky GLSL; source guards cover depth/map wiring."""
import argparse
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "code/renderer_vulkan/shaders"


def cpp(source):
    # Syntax adapters only: swizzles become CPU-vector accessor methods and
    # output arguments become references. The evaluated GLSL math is unchanged.
    return re.sub(r"\.(rgb|xyz|xy)\b", r".\1()", source).replace("out vec4 ", "vec4 &")


def run(compiler):
    integrator = (SHADERS / "pt_integrator.glsl").read_text()
    native = (ROOT / "code/renderer_vulkan/vk_pathtrace.c").read_text()
    guide = integrator[integrator.index("bool found=false, reactive=false;"):]
    assert guide.index("if(material.emission.z!=0) break;") < guide.index("found=true;")
    assert "vec3 reprojectionOffset=direction;" in integrator and "float depth=1;" in integrator
    assert "skyRadiance(triangleMaterials[hit.primitive]&0xffffu,direction,coneSpread)" in integrator
    assert "addIncident(environment(direction,coneSpread))" in integrator
    assert "pt.sky_material = -1;" in native and "lights->sky_environment[0] = pt.sky_material+1;" in native
    assert "pt.sky_material = material_id(shader);" in native
    blend = (SHADERS / "pt_material_layers.glsl").read_text().split("int materialBaseLayer(")[0]
    projection = (SHADERS / "pt_sky_projection.glsl").read_text()
    sky = (SHADERS / "pt_sky.glsl").read_text().replace('#include "pt_sky_projection.glsl"', projection)
    with tempfile.TemporaryDirectory(prefix="pt-sky-") as folder:
        folder = Path(folder)
        (folder / "pt_sky_blending.inc").write_text(cpp(blend))
        (folder / "pt_sky.inc").write_text(cpp(sky))
        executable = folder / "sky-test.exe"
        subprocess.run([compiler, str(ROOT / "tests/pt_sky_fixture.cpp"), "-O1", "-I", str(folder),
                        "-o", str(executable)], check=True, timeout=30)
        subprocess.run([str(executable)], check=True, timeout=10)
    print("PASS: source guards for infinite sky depth/rotation-only motion, per-hit sky and map-reset environment")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=shutil.which("g++") or "g++")
    run(parser.parse_args().cxx)
