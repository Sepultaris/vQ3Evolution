"""Offline exposure UI behavior and renderer wiring; never launches the game."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(cc):
    menu = (ROOT / "code/q3_ui/ui_video.c").read_text()
    renderer = ROOT / "code/renderer_vulkan"
    cvars = (renderer / "tr_cvar.c").read_text()
    native = (renderer / "vk_pathtrace.c").read_text()
    # Existing archived linear exposure remains live, not a restart-only setting.
    assert '"r_pathTracingExposure", "1", CVAR_ARCHIVE)' in cvars
    assert "Cvar_CheckRange(r_pathTracingExposure, 0.01f, 16.0f, qfalse)" in cvars
    assert "c[19] = r_pathTracingExposure->value;" in native
    for name in ("pt_integrator.glsl", "pt_denoise.comp"):
        shader = (renderer / "shaders" / name).read_text()
        assert "radiance*pc.sunExposure.w" in shader
    # Keep custom console values when opening the menu or applying other changes.
    assert menu.count('trap_Cvar_SetValue( "r_pathTracingExposure",') == 1
    for field in ("rtshadowstrength", "ptexposure", "ptexposurereset"):
        assert re.search(rf"{field}\.generic\.y\s*= y;", menu)
        assert f"Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.{field} );" in menu
    row = menu.split("s_graphicsoptions.rtshadowstrength.maxvalue")[1].split("s_graphicsoptions.hudscale.generic.type")[0]
    assert row.count("y +=") == 1, "Exposure must not push other menu controls off-screen"
    print("PASS: live archived exposure reaches both output paths; controls share the RTX row")

    with tempfile.TemporaryDirectory(prefix="pt-exposure-") as folder:
        # Compile the real types, mapping and callbacks, with no unrelated menu
        # dependencies. Function bodies are not duplicated or translated.
        code = menu[menu.index("#define ID_BACK2"):menu.index("} graphicsoptions_t;")+len("} graphicsoptions_t;")]
        code += "\nstatic graphicsoptions_t s_graphicsoptions;\n"
        code += menu[menu.index("static float GraphicsOptions_ExposureForStep("):menu.index("static void GraphicsOptions_UpdateMenuItems(")]
        code += menu[menu.index("static void GraphicsOptions_ExposureEvent("):menu.index("static void GraphicsOptions_ExposureStatus(")]
        (Path(folder) / "pt_exposure_code.inc").write_text(code)
        executable = Path(folder) / ("exposure.exe" if os.name == "nt" else "exposure")
        subprocess.run([cc, str(ROOT / "tests/pt_exposure_fixture.c"), "-O1",
                        "-I", folder,
                        "-lm", "-o", str(executable)], check=True, timeout=30)
        subprocess.run([str(executable)], check=True, timeout=10)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=shutil.which("gcc") or "gcc")
    run(parser.parse_args().cc)
