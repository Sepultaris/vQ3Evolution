"""Run actual environment-material GLSL math on CPU and audit ray-context wiring."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "code/renderer_vulkan/shaders"


def run(compiler):
    source = (SHADERS / "pt_integrator.glsl").read_text()
    assert "layerSample(uint primitive,vec2 bary,int layerIndex,mat2 baryGradients,vec3 observer)" in source
    assert "vec3 viewer=observer-world;" in source
    assert "environmentMaterialUV(normal+dn1*baryGradients[0].x" in source
    assert "environmentMaterialUV(normal+dn1*baryGradients[1].x" in source
    assert "passesAlpha(primitive,bary,origin)" in source
    assert "filterTransmission(primitive,bary,mat2(0),origin)" in source
    assert "emissionAt(hit.primitive,hit.bary,true,origin)" in source
    assert "emissionAt(primitive,bary,false,start)" in source
    assert "baseColor(hit.primitive,hit.bary,origin)" in source
    assert "baseColor(hit.primitive,hit.bary,pc.originNear.xyz)" in source
    # Additive shells must still pass light and guide rays to the solid body.
    assert "materials[id].params.x==2) continue;" in source
    assert "material.params.x!=2 && material.params.x!=3" in source
    with tempfile.TemporaryDirectory(prefix="pt-environment-material-") as folder:
        folder = Path(folder)
        (folder / "pt_environment_material.inc").write_text((SHADERS / "pt_environment_material.glsl").read_text())
        executable = folder / "environment-test.exe"
        subprocess.run([compiler, str(ROOT / "tests/pt_environment_material_fixture.cpp"), "-O1", "-I", str(folder),
                        "-o", str(executable)], check=True, timeout=30)
        subprocess.run([str(executable)], check=True, timeout=10)
    print("PASS: ray-observer wiring for primary/secondary shading, light sampling, alpha, filters and guides; shell transparency retained")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=shutil.which("g++") or "g++")
    run(parser.parse_args().cxx)
