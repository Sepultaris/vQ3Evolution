# VQ3 Evolution

VQ3 Evolution is a modernized Quake III Arena source port centered on a native
Vulkan renderer, correct widescreen presentation, scalable interface rendering,
and optional NVIDIA neural-rendering features. Engine and interface changes are
implemented in source code; they are not shipped as replacement PK3 files.

The project is derived from vkQuake3 and ioquake3 and remains compatible with
Quake III game data, the `baseq3` directory layout, existing mods, and legacy
network protocols.

## Current features

- Vulkan, OpenGL 1, and OpenGL 2 renderers
- Hor+ widescreen and ultrawide field of view without stretching
- Edge-aware HUD placement and independent 50%-150% HUD and UI scaling
- NVIDIA DLSS Super Resolution and DLAA
- DLSS Frame Generation and NVIDIA Reflex
- Experimental DLSS Neural Rendering with live strength controls
- NVIDIA RTX ray-query shadows cast by live world and model geometry
- Experimental native path-traced world lighting (unfinished; see [status](docs/PATH_TRACING.md))
- Separate diffuse/reflection reconstruction, texture-driven PBR materials, and native refractive glass/water
- Native surface-aware denoising with camera/object history and path-hit DLSS inputs
- Animated path materials, layered additive emitters, cutouts and thin transparency (experimental)
- Native Vulkan post-DLSS contrast-adaptive sharpening
- Bilinear, trilinear, and 2x/4x/8x/16x anisotropic texture filtering
- Native source-built game, cgame, and UI modules
- OpenAL audio, Ogg Vorbis/Opus, VoIP, Mumble integration, and SDL 2 input

## Game data

VQ3 Evolution does not include Quake III Arena assets. A legal copy of the game
is required. Place the required `pak*.pk3` files in the `baseq3` directory beside
the built executable. Team Arena data belongs in `missionpack`.

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

## Selecting the renderer

Open the in-game console and enter:

```text
/cl_renderer vulkan
/vid_restart
```

Use `/vkinfo` to confirm that Vulkan is active and to display the selected GPU
and driver information.

## Graphics and interface settings

The Graphics Options menu exposes these controls directly:

| Control | Console setting | Range |
| --- | --- | --- |
| HUD Scale | `cg_hudScale` | 0.5-1.5 |
| UI Scale | `ui_scale` | 0.5-1.5 |
| Texture Filtering | `r_textureMode`, `r_ext_max_anisotropy` | Bilinear through anisotropic 16x |
| NVIDIA RTX Mode | `r_rayTracing` | Off / Shadows / Path tracing (WIP) |
| RTX Shadow Strength | `r_rayTracingShadowStrength` | 0.0-1.0 |
| DLSS Mode | `r_dlss` | Off, Quality, Balanced, Performance, Ultra Performance, DLAA |
| DLSS Sharpness | `r_dlssSharpness` | 0.0-1.0 |
| Neural Rendering | `r_dlssNeuralRendering` | Off or model 1-3 |
| NR Intensity | `r_dlssNRIntensity` | 0.0-2.0 |
| NR Local Tone | `r_dlssNRLocalToneStrength` | 0.0-2.0 |
| NR Local Structure | `r_dlssNRLocalStructureStrength` | 0.0-2.0 |
| NR Skin Structure | `r_dlssNRSkinStructureStrength` | 0.0-2.0 |
| Frame Generation | `r_dlssFrameGeneration` | Off/On |
| NVIDIA Reflex | `r_reflex` | Off, On, On + Boost |

HUD, UI, sharpening, RTX shadow strength, and Neural Rendering strength controls
update live. Renderer mode changes, including enabling RTX shadows, apply after a
renderer restart. See [docs/RTX.md](docs/RTX.md) for hardware requirements,
architecture, controls, and current scope.

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
- `misc`: inherited platform and packaging support

Generated files belong under `build` or a `build-*` directory and must not be
committed. `Makefile.local` is reserved for developer-specific build settings.

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
