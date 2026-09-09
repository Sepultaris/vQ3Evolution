#!/usr/bin/env python3
"""Batch upscale Quake 3 textures and build a drop-in override PK3.

The neural network is deliberately external to this program.  VOSR 2.0 is the
default high-quality backend; Real-ESRGAN and Lanczos remain useful fallbacks.
"""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Iterable

import numpy as np
from PIL import Image, UnidentifiedImageError


SUPPORTED_EXTENSIONS = {".tga", ".jpg", ".jpeg", ".png", ".pcx", ".bmp"}
PROJECT_ROOT = Path(__file__).resolve().parents[2]
UI_ROOTS = ("gfx/", "menu/", "ui/", "fonts/")
NON_COLOR_SUFFIXES = (
    "_normal", "_norm", "_n", "_orm", "_rma", "_rough", "_roughness",
    "_metal", "_metallic", "_height", "_disp", "_displacement", "_ao",
    "_spec", "_specular",
)


@dataclass(frozen=True)
class Asset:
    virtual_path: str
    source: str
    payload: bytes


@dataclass
class Job:
    asset: Asset
    key: str
    extension: str
    rgba: Image.Image
    input_width: int
    input_height: int
    output_width: int
    output_height: int
    has_alpha: bool
    seamless: bool
    padding: int


def normalize_virtual_path(value: str) -> str:
    value = value.replace("\\", "/").lstrip("/")
    path = PurePosixPath(value)
    if not value or path.is_absolute() or ".." in path.parts:
        raise ValueError(f"unsafe virtual path: {value!r}")
    return str(path)


def read_inputs(inputs: list[Path]) -> list[Asset]:
    """Read inputs in order, with later inputs overriding earlier PK3 entries."""
    assets: dict[str, Asset] = {}
    for source in inputs:
        source = source.resolve()
        if source.is_dir():
            for path in sorted(source.rglob("*")):
                if not path.is_file() or path.suffix.lower() not in SUPPORTED_EXTENSIONS:
                    continue
                virtual_path = normalize_virtual_path(path.relative_to(source).as_posix())
                assets[virtual_path.casefold()] = Asset(virtual_path, str(source), path.read_bytes())
        elif source.is_file() and source.suffix.lower() == ".pk3":
            with zipfile.ZipFile(source) as archive:
                for entry in archive.infolist():
                    if entry.is_dir():
                        continue
                    try:
                        virtual_path = normalize_virtual_path(entry.filename)
                    except ValueError:
                        continue
                    if Path(virtual_path).suffix.lower() not in SUPPORTED_EXTENSIONS:
                        continue
                    assets[virtual_path.casefold()] = Asset(
                        virtual_path, str(source), archive.read(entry)
                    )
        elif source.is_file() and source.suffix.lower() in SUPPORTED_EXTENSIONS:
            virtual_path = normalize_virtual_path(source.name)
            assets[virtual_path.casefold()] = Asset(virtual_path, str(source), source.read_bytes())
        else:
            raise ValueError(f"input is not an image, directory, or PK3: {source}")
    return sorted(assets.values(), key=lambda item: item.virtual_path.casefold())


def is_non_color_map(virtual_path: str) -> bool:
    stem = PurePosixPath(virtual_path).stem.casefold()
    return any(stem.endswith(suffix) for suffix in NON_COLOR_SUFFIXES)


def eligibility_reason(asset: Asset, args: argparse.Namespace) -> str | None:
    path = asset.virtual_path.casefold()
    if args.include and not any(fnmatch.fnmatch(path, pattern.casefold()) for pattern in args.include):
        return "not included by pattern"
    if any(fnmatch.fnmatch(path, pattern.casefold()) for pattern in args.exclude):
        return "excluded by pattern"
    if not args.include_ui and path.startswith(UI_ROOTS):
        return "UI/font image (use --include-ui to override)"
    if not args.include_non_color and is_non_color_map(path):
        return "non-color material map (use --include-non-color to override)"
    return None


def open_rgba(asset: Asset) -> Image.Image:
    with Image.open(io.BytesIO(asset.payload)) as image:
        image.load()
        return image.convert("RGBA")


def target_dimensions(width: int, height: int, scale: int, max_size: int) -> tuple[int, int]:
    factor = min(float(scale), max_size / max(width, height))
    if factor <= 1.0:
        return width, height
    return max(1, round(width * factor)), max(1, round(height * factor))


def likely_seamless(rgba: Image.Image) -> bool:
    """Conservatively detect matching opposite borders in color and alpha."""
    pixels = np.asarray(rgba, dtype=np.float32)
    if min(pixels.shape[:2]) < 8:
        return False
    horizontal_edge = np.mean(np.abs(pixels[:, 0] - pixels[:, -1]))
    vertical_edge = np.mean(np.abs(pixels[0, :] - pixels[-1, :]))
    horizontal_local = np.mean(np.abs(pixels[:, 1:] - pixels[:, :-1]))
    vertical_local = np.mean(np.abs(pixels[1:, :] - pixels[:-1, :]))
    return (
        horizontal_edge <= max(10.0, horizontal_local * 1.35)
        and vertical_edge <= max(10.0, vertical_local * 1.35)
    )


def shifted(array: np.ndarray, dy: int, dx: int, wrap: bool) -> np.ndarray:
    result = np.roll(array, (dy, dx), axis=(0, 1))
    if wrap:
        return result
    if dy > 0:
        result[:dy] = 0
    elif dy < 0:
        result[dy:] = 0
    if dx > 0:
        result[:, :dx] = 0
    elif dx < 0:
        result[:, dx:] = 0
    return result


def bleed_transparent_rgb(rgba: Image.Image, wrap: bool, iterations: int = 16) -> Image.Image:
    """Extend edge colors under transparency so neural output has no dark fringe."""
    pixels = np.asarray(rgba, dtype=np.uint8)
    rgb = pixels[:, :, :3].astype(np.float32)
    valid = pixels[:, :, 3] > 0
    if valid.all() or not valid.any():
        return Image.fromarray(rgb.astype(np.uint8), "RGB")

    directions = ((-1, -1), (-1, 0), (-1, 1), (0, -1),
                  (0, 1), (1, -1), (1, 0), (1, 1))
    for _ in range(iterations):
        if valid.all():
            break
        accum = np.zeros_like(rgb)
        count = np.zeros(valid.shape, dtype=np.float32)
        for dy, dx in directions:
            neighbor_valid = shifted(valid, dy, dx, wrap)
            neighbor_rgb = shifted(rgb, dy, dx, wrap)
            accum += neighbor_rgb * neighbor_valid[:, :, None]
            count += neighbor_valid
        fill = ~valid & (count > 0)
        if not fill.any():
            break
        rgb[fill] = accum[fill] / count[fill, None]
        valid[fill] = True
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB")


def wrap_pad(image: Image.Image, amount: int) -> Image.Image:
    if amount <= 0:
        return image
    array = np.asarray(image)
    pad_width = ((amount, amount), (amount, amount))
    if array.ndim == 3:
        pad_width += ((0, 0),)
    padded = np.pad(array, pad_width, mode="wrap")
    return Image.fromarray(padded, image.mode)


def stage_jobs(jobs: list[Job], stage_input: Path) -> None:
    stage_input.mkdir(parents=True)
    for job in jobs:
        rgb = bleed_transparent_rgb(job.rgba, job.seamless)
        if job.padding:
            rgb = wrap_pad(rgb, job.padding)
        rgb.save(stage_input / f"{job.key}.png", format="PNG")


def find_upscaler(value: str | None) -> Path:
    candidates: Iterable[str]
    if value:
        candidates = (value,)
    else:
        candidates = (
            "realesrgan-ncnn-vulkan.exe",
            "realesrgan-ncnn-vulkan",
            str(Path("build-texture-upscaler/realesrgan-ncnn-vulkan.exe")),
        )
    for candidate in candidates:
        found = shutil.which(candidate)
        if found:
            return Path(found).resolve()
        path = Path(candidate)
        if path.is_file():
            return path.resolve()
    raise FileNotFoundError(
        "Real-ESRGAN executable not found; pass --upscaler or run "
        "tools/setup-texture-upscaler.ps1"
    )


def run_backend(args: argparse.Namespace, stage_input: Path, stage_output: Path) -> list[str]:
    stage_output.mkdir(parents=True)
    if args.backend == "lanczos":
        for source in stage_input.glob("*.png"):
            with Image.open(source) as image:
                image.resize(
                    (image.width * args.model_scale, image.height * args.model_scale),
                    Image.Resampling.LANCZOS,
                ).save(stage_output / source.name, format="PNG")
        return ["internal-lanczos"]

    if args.backend == "vosr":
        vosr_dir = (args.vosr_dir or PROJECT_ROOT / "build-texture-upscaler/VOSR").resolve()
        vosr_python = (
            args.vosr_python
            or PROJECT_ROOT / "build-texture-upscaler/vosr-env/Scripts/python.exe"
        ).resolve()
        checkpoint = args.vosr_checkpoint
        if not checkpoint.is_absolute():
            checkpoint = vosr_dir / checkpoint
        inference_script = vosr_dir / "inference_vosr_onestep.py"
        if not vosr_python.is_file():
            raise FileNotFoundError(f"VOSR Python environment not found: {vosr_python}")
        if not inference_script.is_file():
            raise FileNotFoundError(f"VOSR inference script not found: {inference_script}")
        if not checkpoint.is_dir():
            raise FileNotFoundError(f"VOSR checkpoint not found: {checkpoint}")
        command = [
            str(vosr_python), str(inference_script),
            "-c", str(checkpoint.resolve()),
            "-i", str(stage_input.resolve()),
            "-o", str(stage_output.resolve()),
            "-u", str(args.model_scale),
            "--tile_size", str(args.tile or 512),
            "--tile_overlap", str(args.tile_overlap),
            "--vae_tile_size", str(args.vae_tile),
            "--vae_tile_overlap", str(args.vae_tile_overlap),
            "--seed", str(args.seed),
            "--force_rerun",
        ]
        environment = os.environ.copy()
        triton_cache = PROJECT_ROOT / "build-texture-upscaler/triton-cache"
        inductor_cache = PROJECT_ROOT / "build-texture-upscaler/torchinductor-cache"
        triton_cache.mkdir(parents=True, exist_ok=True)
        inductor_cache.mkdir(parents=True, exist_ok=True)
        environment.setdefault("TRITON_CACHE_DIR", str(triton_cache))
        environment.setdefault("TORCHINDUCTOR_CACHE_DIR", str(inductor_cache))
        environment.setdefault("TORCH_ALLOW_TF32_CUBLAS_OVERRIDE", "1")
        subprocess.run(command, cwd=vosr_dir, check=True, env=environment)
        return command

    executable = find_upscaler(args.upscaler)
    command = [
        str(executable), "-i", str(stage_input), "-o", str(stage_output),
        "-n", args.model, "-s", str(args.model_scale), "-f", "png",
        "-t", str(args.tile), "-g", str(args.gpu), "-j", args.threads,
    ]
    models = executable.parent / "models"
    if models.is_dir():
        command.extend(("-m", str(models)))
    if args.tta:
        command.append("-x")
    subprocess.run(command, cwd=executable.parent, check=True)
    return command


def upscale_alpha(job: Job, model_scale: int) -> Image.Image:
    alpha = job.rgba.getchannel("A")
    if job.padding:
        padded = wrap_pad(alpha, job.padding)
        padded = padded.resize(
            (padded.width * model_scale, padded.height * model_scale),
            Image.Resampling.LANCZOS,
        )
        crop = job.padding * model_scale
        alpha = padded.crop((crop, crop, padded.width - crop, padded.height - crop))
    alpha = alpha.resize((job.output_width, job.output_height), Image.Resampling.LANCZOS)
    return alpha


def encode_result(job: Job, neural_path: Path, model_scale: int) -> bytes:
    with Image.open(neural_path) as image:
        image.load()
        rgb = image.convert("RGB")
    if job.padding:
        crop = job.padding * model_scale
        rgb = rgb.crop((crop, crop, rgb.width - crop, rgb.height - crop))
    rgb = rgb.resize((job.output_width, job.output_height), Image.Resampling.LANCZOS)
    result = rgb.convert("RGBA")
    if job.has_alpha:
        result.putalpha(upscale_alpha(job, model_scale))

    output = io.BytesIO()
    extension = job.extension
    if extension in (".jpg", ".jpeg"):
        result.convert("RGB").save(output, format="JPEG", quality=95, subsampling=0, optimize=True)
    elif extension == ".tga":
        mode = "RGBA" if job.has_alpha else "RGB"
        result.convert(mode).save(output, format="TGA", compression="tga_rle")
    elif extension == ".pcx":
        result.convert("RGB").save(output, format="PCX")
    elif extension == ".bmp":
        result.convert("RGBA" if job.has_alpha else "RGB").save(output, format="BMP")
    else:
        result.save(output, format="PNG", optimize=True)
    return output.getvalue()


def write_outputs(output: Path, converted: dict[str, bytes], force: bool) -> None:
    if output.exists() and not force:
        raise FileExistsError(f"output already exists (use --force): {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.suffix.lower() == ".pk3":
        temporary = output.with_suffix(output.suffix + ".tmp")
        if temporary.exists():
            temporary.unlink()
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for virtual_path, payload in sorted(converted.items()):
                archive.writestr(virtual_path, payload)
        os.replace(temporary, output)
        return

    output.mkdir(parents=True, exist_ok=True)
    for virtual_path, payload in converted.items():
        destination = output.joinpath(*PurePosixPath(virtual_path).parts)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(payload)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Upscale Quake 3 textures and write an override directory or PK3."
    )
    parser.add_argument("inputs", nargs="+", type=Path, help="PK3, image, or asset directory")
    parser.add_argument("-o", "--output", type=Path, required=True, help="output .pk3 or directory")
    parser.add_argument("--backend", choices=("vosr", "realesrgan", "lanczos"), default="vosr")
    parser.add_argument("--vosr-dir", type=Path, help="path to the official VOSR checkout")
    parser.add_argument("--vosr-python", type=Path, help="Python executable with VOSR dependencies")
    parser.add_argument("--vosr-checkpoint", type=Path, default=Path("preset/ckpts/VOSR2"))
    parser.add_argument("--upscaler", help="path to realesrgan-ncnn-vulkan executable")
    parser.add_argument("--model", default="realesrgan-x4plus")
    parser.add_argument("--scale", type=int, choices=(2, 3, 4), default=4)
    parser.add_argument("--model-scale", type=int, choices=(2, 3, 4), default=4,
                        help="native backend scale; normally leave at 4")
    parser.add_argument("--max-size", type=int, default=2048,
                        help="maximum output dimension (Vulkan renderer currently caps at 2048)")
    parser.add_argument("--tile", type=int, default=0,
                        help="neural tile size; 0 uses 512 for VOSR or auto for ncnn")
    parser.add_argument("--tile-overlap", type=int, default=64,
                        help="VOSR diffusion-tile overlap")
    parser.add_argument("--vae-tile", type=int, default=1024,
                        help="VOSR VAE tile size")
    parser.add_argument("--vae-tile-overlap", type=int, default=128,
                        help="VOSR VAE-tile overlap")
    parser.add_argument("--seed", type=int, default=42, help="VOSR deterministic seed")
    parser.add_argument("--gpu", default="0")
    parser.add_argument("--threads", default="1:2:2", help="ncnn load:process:save threads")
    parser.add_argument("--tta", action="store_true", help="slower test-time augmentation")
    parser.add_argument("--seamless", choices=("auto", "all", "none"), default="auto")
    parser.add_argument("--seam-padding", type=int, default=16)
    parser.add_argument("--include", action="append", default=[], metavar="GLOB")
    parser.add_argument("--exclude", action="append", default=[], metavar="GLOB")
    parser.add_argument("--include-ui", action="store_true")
    parser.add_argument("--include-non-color", action="store_true")
    parser.add_argument("--dry-run", action="store_true", help="inspect and report without inference")
    parser.add_argument("--force", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.max_size < 16:
        raise ValueError("--max-size must be at least 16")
    if args.seam_padding < 0:
        raise ValueError("--seam-padding cannot be negative")
    if args.tile_overlap < 0 or args.vae_tile_overlap < 0:
        raise ValueError("tile overlap cannot be negative")
    if args.vae_tile <= 0:
        raise ValueError("--vae-tile must be positive")

    output_resolved = args.output.resolve()
    for source in args.inputs:
        source_resolved = source.resolve()
        if source_resolved == output_resolved:
            raise ValueError("output must not overwrite an input")
        if source_resolved.is_dir() and output_resolved.suffix.lower() != ".pk3":
            try:
                output_resolved.relative_to(source_resolved)
            except ValueError:
                pass
            else:
                raise ValueError("an output directory must not be inside an input directory")

    started = time.time()
    assets = read_inputs(args.inputs)
    jobs: list[Job] = []
    records: list[dict[str, object]] = []

    for asset in assets:
        record: dict[str, object] = {
            "path": asset.virtual_path,
            "source": asset.source,
            "source_sha256": hashlib.sha256(asset.payload).hexdigest(),
        }
        reason = eligibility_reason(asset, args)
        if reason:
            record.update(status="skipped", reason=reason)
            records.append(record)
            continue
        try:
            rgba = open_rgba(asset)
        except (UnidentifiedImageError, OSError, ValueError) as error:
            record.update(status="failed", reason=f"decode error: {error}")
            records.append(record)
            continue
        target_width, target_height = target_dimensions(
            rgba.width, rgba.height, args.scale, args.max_size
        )
        if (target_width, target_height) == rgba.size:
            record.update(
                status="skipped", reason="already at maximum size",
                input_size=list(rgba.size),
            )
            records.append(record)
            continue
        seamless = args.seamless == "all" or (
            args.seamless == "auto" and likely_seamless(rgba)
        )
        alpha = np.asarray(rgba.getchannel("A"))
        has_alpha = bool(np.any(alpha < 255))
        key = hashlib.sha1(asset.virtual_path.casefold().encode("utf-8")).hexdigest()
        job = Job(
            asset=asset,
            key=key,
            extension=Path(asset.virtual_path).suffix.lower(),
            rgba=rgba,
            input_width=rgba.width,
            input_height=rgba.height,
            output_width=target_width,
            output_height=target_height,
            has_alpha=has_alpha,
            seamless=seamless,
            padding=args.seam_padding if seamless else 0,
        )
        jobs.append(job)
        record.update(
            status="planned" if args.dry_run else "queued",
            input_size=[rgba.width, rgba.height],
            output_size=[target_width, target_height],
            alpha=bool(has_alpha),
            seamless=bool(seamless),
        )
        records.append(record)

    command: list[str] | None = None
    converted: dict[str, bytes] = {}
    if jobs and not args.dry_run:
        with tempfile.TemporaryDirectory(prefix="vq3-textures-") as temporary_name:
            temporary = Path(temporary_name)
            stage_input = temporary / "input"
            stage_output = temporary / "output"
            stage_jobs(jobs, stage_input)
            command = run_backend(args, stage_input, stage_output)
            by_path = {record["path"]: record for record in records}
            for job in jobs:
                result_path = stage_output / f"{job.key}.png"
                record = by_path[job.asset.virtual_path]
                if not result_path.is_file():
                    record.update(status="failed", reason="backend produced no output")
                    continue
                try:
                    converted[job.asset.virtual_path] = encode_result(
                        job, result_path, args.model_scale
                    )
                    record["status"] = "converted"
                except (UnidentifiedImageError, OSError, ValueError) as error:
                    record.update(status="failed", reason=f"encode error: {error}")
        if converted:
            write_outputs(output_resolved, converted, args.force)

    manifest = {
        "tool": "VQ3 Evolution texture upscaler",
        "version": 1,
        "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "inputs": [str(path.resolve()) for path in args.inputs],
        "output": str(output_resolved),
        "backend": args.backend,
        "model": (
            str(args.vosr_checkpoint) if args.backend == "vosr"
            else args.model if args.backend == "realesrgan"
            else None
        ),
        "scale": args.scale,
        "model_scale": args.model_scale,
        "max_size": args.max_size,
        "command": command,
        "duration_seconds": round(time.time() - started, 3),
        "summary": {
            status: sum(record.get("status") == status for record in records)
            for status in ("converted", "planned", "skipped", "failed")
        },
        "textures": records,
    }
    manifest_path = args.output.resolve().with_name(args.output.name + ".manifest.json")
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    summary = manifest["summary"]
    print(
        f"Textures: {summary['converted']} converted, {summary['planned']} planned, "
        f"{summary['skipped']} skipped, {summary['failed']} failed"
    )
    print(f"Manifest: {manifest_path}")
    if converted:
        print(f"Override: {args.output.resolve()}")
    return 1 if summary["failed"] else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, FileExistsError, ValueError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
