# NVIDIA DLSS support

The Windows x64 Vulkan renderer integrates NVIDIA Streamline 2.12.0 directly
in native engine code. Nothing is supplied through a PK3. The integration
provides:

- DLSS Super Resolution: Quality, Balanced, Performance, and Ultra Performance
- DLAA at native rendering resolution
- DLSS Neural Rendering: three runtime-provided model styles
- DLSS Frame Generation (2x)
- NVIDIA Reflex: On and On + Boost

The 3D scene is rendered to dedicated temporal color and depth images at the
resolution recommended by DLSS. Streamline receives per-frame camera matrices,
depth, motion-vector metadata, subpixel jitter, and the HUD-less native output.
When Neural Rendering is enabled, the HUD-less scene first passes through the
neural render transform at the internal render resolution. DLSS Super Resolution
then reconstructs that result at native resolution. The result is copied to the
swapchain before the HUD and menus are drawn at full resolution. UI-only frames
bypass temporal processing, and Frame Generation is disabled while its gameplay
inputs are not valid.

The legacy renderer does not yet emit per-object motion vectors. Its motion
texture uses Streamline's invalid-vector sentinel and declares that camera
motion is not included, allowing DLSS to reconstruct camera motion from depth
and the supplied matrices. Moving-object detail is supplemented by DLSS-G's
optical-flow path.

## Project identity

An NVIDIA-issued application ID is not required for this custom engine path.
Streamline is initialized with a stable, project-owned GUID and the engine
version:

`66b8e39a-d95f-4ed1-a032-dab9717bf73f`

Do not substitute NVIDIA's sample application ID. A publisher integrating this
into a commercial release can still register the title with NVIDIA, but that is
not required for the code in this repository to initialize NGX.

## Getting the SDK and Neural Rendering runtime

NVIDIA's SDK binaries are not stored in the source tree. From PowerShell run:

```powershell
.\tools\fetch-streamline.ps1
```

The script downloads NVIDIA's official 2.12.0 release, verifies the archive's
SHA-256 checksum, and extracts it under `build-widescreen/deps`. A build made
with `USE_NVIDIA_DLSS=1` copies only the required production runtime DLLs and
license files beside the executable.

Neural Rendering currently uses NVIDIA's signed `nvngx_dlssnr.dll` runtime,
which is not part of the public Streamline 2.12 package and is not redistributed
by this repository. Place a legitimately obtained NVIDIA-signed copy beside
`vQ3Evolution.exe`. The build supplies the native Vulkan integration and its
small caller-identity bridge as `nvngx.dll`; it does not patch or replace the
NVIDIA runtime. The renderer refuses to load the neural runtime unless Windows
validates its Authenticode signature and publisher as NVIDIA Corporation.

The present 310.8 runtime exposes Neural Rendering through experimental NGX
feature 18. Because NVIDIA has not published that feature's Vulkan contract yet,
compatibility may change with later runtime or driver versions. If attachment,
creation, or evaluation fails, the renderer keeps the original scene and DLSS
Super Resolution continues safely.

At runtime, the renderer resolves the interposer from an absolute executable-
relative path, restricts dependency lookup, and verifies NVIDIA's embedded
signature before loading it. Streamline also verifies its feature DLLs.

## Building

Use the existing MinGW64 release build with `USE_NVIDIA_DLSS=1`. For example,
from the MSYS2 shell:

```sh
make BUILD_DIR=build-widescreen PLATFORM=mingw64 ARCH=x86_64 \
  BUILD_CLIENT=1 BUILD_SERVER=0 BUILD_GAME_SO=1 BUILD_GAME_QVM=1 \
  BUILD_BASEGAME=1 BUILD_MISSIONPACK=1 BUILD_RENDERER_OPENGL2=1 \
  USE_NVIDIA_DLSS=1 -j4 release
```

The resulting executable is under
`build-widescreen/release-mingw64-x86_64`.

## Settings

The Graphics Options menu exposes all settings. Mode changes apply with a
renderer restart; the four Neural Rendering strength sliders update live, even
while a map is loaded. Their console equivalents are:

| Setting | Values |
| --- | --- |
| `r_dlss` | `0` Off, `1` Quality, `2` Balanced, `3` Performance, `4` Ultra Performance, `5` DLAA |
| `r_dlssSharpness` | Contrast-adaptive post-upscale sharpness, `0.0` to `1.0` (default `0.0`) |
| `r_dlssNeuralRendering` | `0` Off, `1` Model 1, `2` Model 2, `3` Model 3 |
| `r_dlssNRIntensity` | Overall Neural Rendering strength, `0.0` to `2.0` (default `1.0`) |
| `r_dlssNRLocalToneStrength` | Local tone strength, `0.0` to `2.0` (default `1.0`) |
| `r_dlssNRLocalStructureStrength` | Local structure strength, `0.0` to `2.0` (default `1.0`) |
| `r_dlssNRSkinStructureStrength` | Skin structure strength, `0.0` to `2.0` (default `1.0`) |
| `r_dlssFrameGeneration` | `0` Off, `1` On |
| `r_reflex` | `0` Off, `1` On, `2` On + Boost |

The controls are disabled automatically unless the Vulkan renderer and the
corresponding GPU/driver feature are available.

Current DLSS releases do not support Streamline's deprecated built-in
`sharpness` field. `r_dlssSharpness` therefore runs a native Vulkan
contrast-adaptive compute pass on the DLSS output before HUD rendering. It is
live-adjustable and does not alter the temporal input or motion-vector history.

## Runtime requirements and verification

DLSS requires supported NVIDIA RTX hardware and a compatible driver. Frame
Generation also requires supported RTX hardware, Windows Hardware-Accelerated
GPU Scheduling, and a non-VSync presentation path. OpenGL renderers are
unchanged, and Vulkan continues to work normally when the optional NVIDIA
runtime is absent.

With developer logging enabled, successful activation includes messages like:

```text
NVIDIA DLSS Neural Rendering feature created: 1280x720, model 1
NVIDIA DLSS Neural Rendering evaluation active
Created DLSSContext feature (1067,600)(optimal) -> (1600,900)
NVIDIA DLSS Frame Generation active (2 frames presented)
```
