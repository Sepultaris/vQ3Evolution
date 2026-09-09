# VQ3 Evolution

VQ3 Evolution is a modernized Quake III Arena source port centered on a native
Vulkan renderer, correct widescreen presentation, scalable interface rendering,
and optional NVIDIA rendering/reconstruction features. Engine and interface changes are
implemented in source code; they are not shipped as replacement PK3 files.

The project is derived from vkQuake3 and ioquake3 and remains compatible with
Quake III game data, the `baseq3` directory layout, existing mods, and legacy
network protocols.

## Current features

- Vulkan, OpenGL 1, and OpenGL 2 renderers
- Hor+ widescreen and ultrawide field of view without stretching
- Edge-aware HUD placement and independent 50%-150% HUD and UI scaling
- Engine-owned rendering/interface options available in Team Arena and mods,
  with shared settings and compatibility scaling for older modules
- NVIDIA DLSS Super Resolution and DLAA
- Experimental DLSS Frame Generation integration and NVIDIA Reflex
- Experimental DLSS Neural Rendering with live strength controls
- NVIDIA RTX ray-query shadows cast by live world and model geometry
- Experimental native path-traced world lighting (unfinished; see [current status](docs/STATUS.md))
- DLSS Ray Reconstruction, including native-resolution DLAA; requested by default
  when supported path tracing and DLSS/DLAA are selected
- Separate diffuse/reflection reconstruction, texture-driven PBR materials, and native refractive glass/water
- Native surface-aware denoising with camera/object history and path-hit DLSS inputs
- Local lighting-change reconstruction, planar-mirror target motion, and native blue-noise/spatial light sampling
- Animated path materials, layered additive emitters, cutouts and thin transparency (experimental)
- Native Vulkan post-DLSS contrast-adaptive sharpening
- Bilinear, trilinear, and 2x/4x/8x/16x anisotropic texture filtering
- Native source-built game, cgame, and UI modules
- OpenAL audio, Ogg Vorbis/Opus, VoIP, Mumble integration, and SDL 2 input

The path tracer defaults to **two fixed samples**, adaptive sampling off, and
four bounces; saved values are preserved. Ray Reconstruction and experimental
Neural Rendering are different features: NR is bypassed while RR runs.

NVIDIA shutdown, Frame Generation/restart reliability, some Vulkan validation
paths and complex material/motion cases remain open. Recent performance results
include timed-out diagnostics and are not general FPS guarantees. See
[current status](docs/STATUS.md) and the [documentation index](docs/README.md).

## Game data

VQ3 Evolution does not include Quake III Arena assets. A legal copy of the game
is required. Place the required `pak*.pk3` files in the `baseq3` directory beside
the built executable. Team Arena requires its own `missionpack/pak0.pk3` plus
`missionpack/pak1.pk3`, `pak2.pk3`, and `pak3.pk3`. The latter three are the
Team Arena point-release updates; a Steam installation may contain only `pak0`.
Obtain the missing updates from the [official patch-data page](https://ioquake3.org/extras/patch-data/)
after accepting its license, and copy only the archive's `missionpack` packs
into the `missionpack` directory beside `vQ3Evolution.exe`. Do not put the
`baseq3` update packs in `missionpack` or rename them to satisfy the check.

Launch **TEAM ARENA** from the main menu, or start
`vQ3Evolution.exe +set fs_game missionpack`. The source-built Team Arena modules
are included in the normal build; the original game/update assets remain
separate from engine and renderer changes.

Game data, local configurations, screenshots, demos, logs, downloaded SDKs, and
compiled build trees are excluded from Git.

## Building on Windows x64

Windows x64 with the MSYS2 UCRT64 toolchain is the primary development target.
Install MSYS2, open its UCRT64 shell, and install GCC and Make:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc make git
```

Fetch the official NVIDIA Streamline SDK once from PowerShell:

```powershell
.\tools\fetch-streamline.ps1
```

Then build from the repository root in the MSYS2 UCRT64 shell:

```sh
make PLATFORM=mingw64 ARCH=x86_64 BUILD_DIR=build-widescreen \
  USE_NVIDIA_DLSS=1 BUILD_SERVER=0 -j4 release
```

The resulting client is:

```text
build-widescreen/release-mingw64-x86_64/vQ3Evolution.exe
```

The bundled legacy Windows libcurl is incompatible with the current UCRT
toolchain, so HTTP/FTP download support is disabled by default on Windows.
Builders with a compatible current libcurl can explicitly use `USE_CURL=1`.
This does not affect gameplay, multiplayer networking, Vulkan, or DLSS.

### Neural Rendering runtime

DLSS Super Resolution, DLAA, Frame Generation, and Reflex use files obtained by
the Streamline fetch step. Experimental Neural Rendering additionally requires
a legitimately obtained, NVIDIA-signed `nvngx_dlssnr.dll` beside
`vQ3Evolution.exe`. That proprietary DLL is not stored in this repository.

See [docs/DLSS.md](docs/DLSS.md) for architecture, settings, runtime requirements,
and verification details.

For a build without NVIDIA integration, omit the fetch step and set
`USE_NVIDIA_DLSS=0` in the build command. Raster and hardware-capable native
path-tracing paths do not require the optional NVIDIA runtime. The Vulkan SDK
is needed to regenerate shaders, not to link the checked-in embedded payloads;
see [testing/build checks](docs/TESTING.md).

## Selecting the renderer

Open the in-game console and enter:

```text
/cl_renderer vulkan
/vid_restart
```

Use `/vkinfo` to confirm that Vulkan is active and to display the selected GPU
and driver information.

## Graphics and interface settings

Press **Shift+F10** or enter `/vq3e_options` for the engine-owned options panel.
It works in loaded games, Team Arena and mods without replacing their menus.
Display, Lighting, NVIDIA and Interface settings are staged until **Apply**,
and are shared across game folders. Existing menus and console commands remain
available. See [Universal options](docs/UNIVERSAL_OPTIONS.md) for persistence,
legacy scaling, controls and compatibility limits.

True Combat 0.45 has a separate bug in its own widescreen menu calculations.
An optional, version-checked [mod UI patch](docs/TRUECOMBAT_PATCH.md) restores its
cursor and map thumbnails without changing the engine or gameplay modules.

The original base-game Graphics Options menu also exposes these controls:

| Control | Console setting | Range |
| --- | --- | --- |
| HUD Scale | `cg_hudScale` | 0.5-1.5 |
| UI Scale | `ui_scale` | 0.5-1.5 |
| Texture Filtering | `r_textureMode`, `r_ext_max_anisotropy` | Bilinear through anisotropic 16x |
| NVIDIA RTX Mode | `r_rayTracing` | Off / Shadows / Path tracing (WIP) |
| RTX Shadow Strength | `r_rayTracingShadowStrength` | 0.0-1.0 |
| RTX Exposure (Path tracing mode) | `r_pathTracingExposure` | 0.0625x-16x, quarter-stop slider; Reset = 1x |
| DLSS Mode | `r_dlss` | Off, Quality, Balanced, Performance, Ultra Performance, DLAA |
| DLSS Ray Reconstruction | `r_dlssRayReconstruction` | Off/On; path tracing plus supported DLSS/DLAA required |
| DLSS Sharpness | `r_dlssSharpness` | 0.0-1.0 |
| Neural Rendering | `r_dlssNeuralRendering` | Off or model 1-3 |
| NR Intensity | `r_dlssNRIntensity` | 0.0-2.0 |
| NR Local Tone | `r_dlssNRLocalToneStrength` | 0.0-2.0 |
| NR Local Structure | `r_dlssNRLocalStructureStrength` | 0.0-2.0 |
| NR Skin Structure | `r_dlssNRSkinStructureStrength` | 0.0-2.0 |
| Frame Generation | `r_dlssFrameGeneration` | Off/On |
| NVIDIA Reflex | `r_reflex` | Off, On, On + Boost |

HUD, UI, sharpening, RTX exposure/shadow strength, and Neural Rendering strength controls
update live. Renderer mode changes, including enabling RTX shadows, apply after a
renderer restart. See [docs/RTX.md](docs/RTX.md) for hardware requirements,
architecture, controls, and current scope.

In Path tracing mode, **RTX Exposure** occupies the shadow-strength row. Raise it
to brighten the scene (2x is one stop brighter, 4x is two); **Reset** restores 1x.
It affects scene tone mapping, not the HUD or menu brightness, and is saved
automatically. You can also enter `/r_pathTracingExposure 2` in the console for
an immediate adjustment, with no renderer restart.

## Linux and macOS

The inherited Linux and macOS build paths remain available. Install the platform
development packages for SDL 2, OpenAL, Vulkan, OpenGL, Opus, Vorbis, and libcurl,
then run `make -j4`. The client is named `vQ3Evolution.<architecture>` on these
platforms. NVIDIA Streamline integration is currently limited to Windows x64.

## Repository layout

- `code/renderer_vulkan`: Vulkan renderer, RTX, temporal pipeline, DLSS, and sharpening
- `code/q3_ui`: source-built Quake III menus and graphics settings
- `code/cgame`: client game and HUD code
- `code/game`: server-side game code
- `docs`: VQ3 Evolution feature documentation and third-party notices
- `tools`: project build and dependency helpers
- `tests`: offline fixtures, shader checks and opt-in GPU scenarios
- `misc`: inherited platform and packaging support

Build output belongs under `build` or a `build-*` directory and must not be
committed. The deliberate exceptions are the embedded shader payloads under
`code/renderer_vulkan/shaders/Compiled` and the generated blue-noise source
header: they are build inputs kept in sync with their generators/sources.
`Makefile.local` is reserved for developer-specific build settings. Follow the
[commit checks](docs/TESTING.md#repository-checks) before staging changes.

## Upstream lineage and licensing

VQ3 Evolution contains work from id Software, ioquake3, vkQuake3, and their
contributors. Some inherited source comments and component documents retain
their upstream names where that history or compatibility context matters.

The engine source is distributed under the GNU General Public License v2; see
[COPYING.txt](COPYING.txt). Quake III game assets and NVIDIA runtime binaries
have separate licenses and are not part of the source repository. The original
id Software source-release notes are preserved in [id-readme.txt](id-readme.txt).
The vendored Vulkan API headers retain their Khronos Apache-2.0 notices.

Upstream references:

- [ioquake3](https://ioquake3.org/)
- [vkQuake3 source lineage](https://github.com/suijingfeng/vkQuake3)
