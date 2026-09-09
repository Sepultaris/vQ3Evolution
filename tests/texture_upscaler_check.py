"""Offline tests for the Quake-aware texture upscaling pipeline."""

from __future__ import annotations

import io
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "texture_upscaler" / "upscale_textures.py"


def image_bytes(mode: str, size: tuple[int, int], color: tuple[int, ...], fmt: str) -> bytes:
    output = io.BytesIO()
    Image.new(mode, size, color).save(output, format=fmt)
    return output.getvalue()


class TextureUpscalerTests(unittest.TestCase):
    def test_pk3_round_trip_policy_alpha_and_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_name:
            temporary = Path(temporary_name)
            source = temporary / "pak0.pk3"
            output = temporary / "z-test-hd.pk3"
            rgba = Image.new("RGBA", (8, 8), (220, 80, 20, 0))
            for y in range(2, 6):
                for x in range(2, 6):
                    rgba.putpixel((x, y), (220, 80, 20, 255))
            rgba_bytes = io.BytesIO()
            rgba.save(rgba_bytes, format="TGA")
            with zipfile.ZipFile(source, "w") as archive:
                archive.writestr("textures/test/cutout.tga", rgba_bytes.getvalue())
                archive.writestr("gfx/2d/bigchars.tga", image_bytes("RGB", (8, 8), (255, 255, 255), "TGA"))
                archive.writestr("textures/test/wall_normal.png", image_bytes("RGB", (8, 8), (128, 128, 255), "PNG"))

            subprocess.run(
                [sys.executable, str(TOOL), str(source), "-o", str(output),
                 "--backend", "lanczos", "--scale", "2", "--max-size", "64"],
                check=True,
            )

            with zipfile.ZipFile(output) as archive:
                self.assertEqual(archive.namelist(), ["textures/test/cutout.tga"])
                with Image.open(io.BytesIO(archive.read("textures/test/cutout.tga"))) as result:
                    self.assertEqual(result.size, (16, 16))
                    alpha = np.asarray(result.convert("RGBA").getchannel("A"))
                    self.assertEqual(int(alpha[0, 0]), 0)
                    self.assertGreater(int(alpha[8, 8]), 200)

            manifest = json.loads(Path(str(output) + ".manifest.json").read_text("utf-8"))
            self.assertEqual(manifest["summary"]["converted"], 1)
            self.assertEqual(manifest["summary"]["skipped"], 2)

    def test_dry_run_does_not_require_backend(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_name:
            temporary = Path(temporary_name)
            source = temporary / "textures"
            source.mkdir()
            (source / "stone.png").write_bytes(image_bytes("RGB", (16, 8), (90, 80, 70), "PNG"))
            output = temporary / "planned.pk3"
            subprocess.run(
                [sys.executable, str(TOOL), str(source), "-o", str(output), "--dry-run"],
                check=True,
            )
            self.assertFalse(output.exists())
            manifest = json.loads(Path(str(output) + ".manifest.json").read_text("utf-8"))
            self.assertEqual(manifest["summary"]["planned"], 1)

    def test_refuses_to_overwrite_an_input(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_name:
            source = Path(temporary_name) / "stone.png"
            source.write_bytes(image_bytes("RGB", (8, 8), (90, 80, 70), "PNG"))
            result = subprocess.run(
                [sys.executable, str(TOOL), str(source), "-o", str(source),
                 "--backend", "lanczos", "--force"],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 2)
            self.assertIn("must not overwrite an input", result.stderr)


if __name__ == "__main__":
    unittest.main()
