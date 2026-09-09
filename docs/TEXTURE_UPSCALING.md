# Offline texture upscaling

VQ3 Evolution includes an offline remaster pipeline that reads loose assets or
Quake 3 PK3 archives, runs color textures through a neural upscaler, and writes
an override PK3. Its size depends on texture count and resolution and can be
substantial. The neural model is not linked into the renderer and no
original game asset is committed to this repository.

## Why this is offline

Neural texture inference is expensive, nondeterministic across model versions,
and unnecessary while the game is running. An override PK3 is faster to load,
easy to remove, and works with the engine's existing virtual filesystem. The
output keeps every original virtual path and file format, including explicit
`.tga` shader references.

The pinned default backend is VOSR 2.0, a one-step 1.4-billion-parameter
vision-only restoration model. It runs offline through PyTorch/CUDA and is much
heavier than a real-time scaler. The older portable Real-ESRGAN backend remains
available as a fast fallback.

Finish or stop offline inference before benchmarking the game: it competes for
the same GPU and VRAM and previously caused misleading sub-1-FPS observations.
Compare the same installed texture pack and filtering settings in both runs.
Renderer/UI features remain native code; this optional pack changes assets only.

## Setup

Python 3.10 or newer is recommended. Install the two orchestration dependencies:

```powershell
py -m pip install -r tools/texture_upscaler/requirements.txt
```

For an NVIDIA RTX GPU, fetch the pinned official VOSR 2.0 source and weights,
create an isolated environment, and install a Blackwell-compatible CUDA build:

```powershell
.\tools\setup-vosr-texture-upscaler.ps1
```

The model bundle is roughly 7 GB. The script verifies that CUDA inference is
available before it finishes. To install the lightweight legacy backend instead:

```powershell
.\tools\setup-texture-upscaler.ps1
```

Both setups are placed in the ignored `build-texture-upscaler` directory.

## Inspect before processing

Pass PK3 files in their normal load order. Later archives override earlier ones,
matching Quake's asset behavior:

```powershell
py tools/texture_upscaler/upscale_textures.py `
  C:\Games\Quake3\baseq3\pak0.pk3 `
  C:\Games\Quake3\baseq3\pak1.pk3 `
  --output build-texture-upscaler\z-vq3-hd.pk3 `
  --dry-run
```

The dry run writes `z-vq3-hd.pk3.manifest.json`. Review its planned and skipped
textures before starting the longer GPU job.

## Build an override PK3

```powershell
py tools/texture_upscaler/upscale_textures.py `
  C:\Games\Quake3\baseq3\pak0.pk3 `
  --output build-texture-upscaler\z-vq3-hd.pk3
```

Copy the resulting PK3 into `baseq3`, after retaining the manifest alongside the
build artifacts. Its `z-` prefix ensures that it loads after the stock packages.
Remove that one PK3 to return to the original textures.

Set `r_picmip 0` and restart the renderer when reviewing the pack. The Vulkan
renderer defaults to `r_picmip 1`, which discards one mip level and would hide
half of the new linear resolution.

Useful controls:

- `--scale 2` makes a smaller pack and is a conservative first pass.
- `--max-size 2048` matches the Vulkan renderer's current texture limit.
- `--include "textures/gothic_*/*"` limits a test run to one family.
- `--exclude "*/skies/*"` may be repeated for unwanted groups.
- `--seamless auto` wrap-pads textures whose opposite borders already match.
- `--tile 256` lowers VOSR peak GPU memory; the default is 512.
- `--tile-overlap 64` and `--vae-tile-overlap 128` reduce tile-boundary artifacts.
- `--seed 42` keeps VOSR output repeatable.
- `--backend realesrgan` selects the older lightweight backend; `--tta` applies there.
- `--backend lanczos` tests packaging without invoking a neural model.

## Quake-specific safeguards

The default policy intentionally skips `gfx`, `menu`, `ui`, and `fonts` because
generative models deform text and crisp interface art. It also skips filenames
that look like normal, ORM, roughness, metalness, specular, height,
displacement, or AO maps; an RGB restoration network must not reinterpret
vector or scalar data.

For transparent textures, RGB color is extended underneath transparent pixels
before inference and the alpha mask is resampled separately. This reduces dark
halos while preventing the model from inventing coverage. Likely repeating
textures receive wrapped context before inference, then are cropped back to the
original periodic boundary.

Every run emits a JSON manifest containing source hashes, dimensions, seam and
alpha decisions, skips, failures, the backend command, and final counts. Treat
the generated pack as an art baseline: inspect signs, weapon skins, character
faces, texture atlases, skies, and prominent surfaces in game before release.
