# VQ3 Evolution NVIDIA DLSS support

The Windows x64 Vulkan renderer integrates NVIDIA Streamline 2.12.0 directly
in native engine code. Nothing is supplied through a PK3. The integration
provides:

- DLSS Super Resolution: Quality, Balanced, Performance, and Ultra Performance
- DLAA at native rendering resolution
- DLSS Ray Reconstruction for the path tracer, including native-resolution DLAA
- WIP, console-only DLSS Neural Rendering: three runtime-provided model styles
- Experimental DLSS Frame Generation (2x-6x, subject to GPU/runtime capability)
- NVIDIA Reflex: On and On + Boost

The 3D scene is rendered to dedicated temporal color and depth images at the
resolution recommended by DLSS. Streamline receives per-frame camera matrices,
depth, motion-vector metadata, subpixel jitter, and the HUD-less native output.
When RR is not active and Neural Rendering is enabled, the HUD-less scene first passes through the
neural render transform at the internal render resolution. DLSS Super Resolution
then reconstructs that result at native resolution. The result is copied to the
swapchain before the HUD and menus are drawn at full resolution. UI-only frames
bypass temporal processing, and Frame Generation is disabled while its gameplay
inputs are not valid.

The 2D draw path selects the full-resolution UI target on every frame, including
consecutive menu-only frames. It must not gate this transition on the previous
draw's `projection2D` flag: that flag can remain true across frames while the
temporal scene target has restarted. That mismatch previously rendered menus
into the smaller scene image, then enlarged/clipped them during presentation
(1080p Quality shifted the menu center from x=960 to x=1440). The native fix
does not change UI coordinates, scaling preferences or mouse hit boxes.
`pt_ui_position.cfg` exercises Setup, Graphics and return to gameplay;
`pt_ui_position_check.py` checks captured placement and compiles 48 real
draw-path cases, with the previous implementation as a failing negative control.

When Frame Generation is off at renderer startup, the engine unloads only the
DLSS-G presentation hooks after querying device support and before creating
the swapchain, as specified by Streamline 2.12's DLSS-G guide (section 18).
DLAA, Ray Reconstruction, Neural Rendering and Reflex remain loaded. FG remains available in the
menu; changing its latched setting recreates the renderer and swapchain. This
avoids an unused FG proxy swapchain and pacing queue. If the optional unload
call fails, the original valid interfaces are retained. The diagnostic
`r_dlssFGIdleHooks 1` keeps the former hook route for comparison.

A 2026-09-08 bounded FG-off validation run with the Vulkan validation layer confirmed
loaded completed cache switches, camera movement and shutdown without any
warnings or errors. The earlier presentation layout/semaphore errors did not
recur in that test. This does not establish FG-on validation, and the measured
FG-off frame time was unchanged. More recent shutdown failures remain open;
see [current status](STATUS.md), not the older pass alone.

The raster renderer does not yet emit per-object motion vectors. Its motion
texture uses Streamline's invalid-vector sentinel and declares that camera
motion is not included, allowing DLSS to reconstruct camera motion from depth
and the supplied matrices. Moving-object detail is supplemented by DLSS-G's
optical-flow path.

The path tracer supplies physical surface depth and camera-plus-object motion
from previous tracked vertices. RR additionally receives linear diffuse albedo,
directional-hemispherical specular albedo, world-space shading normals, linear
roughness and specular hit distance. See [Ray Reconstruction](RAY_RECONSTRUCTION.md)
for the native HDR pipeline, measured DLAA results and remaining limitations.

## Project identity

An NVIDIA-issued application ID is not required for this custom engine path.
Streamline is initialized with a stable, project-owned GUID and the engine
version:

`66b8e39a-d95f-4ed1-a032-dab9717bf73f`

This describes the public Streamline SR/RR identity path. The experimental NR
snippet is a separate, version-specific bridge integration and should not be
represented as a public DLSS 5 SDK contract. Do not replace the project GUID
with another game's identity or treat initialization as distribution approval.
Runtime distribution remains subject to the applicable NVIDIA license.

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
which is not part of the pinned Streamline 2.12 package and is not redistributed
by this repository. Place a legitimately obtained NVIDIA-signed copy beside
`vQ3Evolution.exe`. The build supplies the native Vulkan integration and its
small caller-identity bridge as `nvngx.dll`; it does not patch or replace the
NVIDIA runtime. The renderer refuses to load the neural runtime unless Windows
validates its Authenticode signature and publisher as NVIDIA Corporation.

The tested 310.8 runtime exposes Neural Rendering through experimental NGX
feature 18. This version-specific integration is outside the pinned public SDK
contract; compatibility may change with later runtime or driver versions. If attachment,
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

The resulting executable is
`build-widescreen/release-mingw64-x86_64/vQ3Evolution.exe`.

## Settings

The Graphics Options and universal engine menus expose DLSS, RR, sharpening,
Frame Generation and Reflex. Mode/multiplier changes apply with a renderer
restart. Neural Rendering and its tuning are WIP and console-only; its four
strength commands still update live. Opening or applying either menu preserves
these hidden settings. Console controls:

| Setting | Values |
| --- | --- |
| `r_dlss` | `0` Off, `1` Quality, `2` Balanced, `3` Performance, `4` Ultra Performance, `5` DLAA |
| `r_dlssRayReconstruction` | `0` Native reconstruction, `1` NVIDIA RR (default); requires path tracing and a DLSS/DLAA mode; restart to apply |
| `r_dlssSharpness` | Contrast-adaptive post-upscale sharpness, `0.0` to `1.0` (default `0.0`) |
| `r_dlssNeuralRendering` | WIP, console-only: `0` Off, `1` Model 1, `2` Model 2, `3` Model 3 |
| `r_dlssNRIntensity` | Overall Neural Rendering strength, `0.0` to `2.0` (default `1.0`) |
| `r_dlssNRLocalToneStrength` | Local tone strength, `0.0` to `2.0` (default `1.0`) |
| `r_dlssNRLocalStructureStrength` | Local structure strength, `0.0` to `2.0` (default `1.0`) |
| `r_dlssNRSkinStructureStrength` | Skin structure strength, `0.0` to `2.0` (default `1.0`) |
| `r_dlssFrameGeneration` | `0` Off, `1` On |
| `r_dlssFrameGenerationMultiplier` | Integer `2`-`6`, default `2`; total target frames per rendered frame; restart required |
| `r_dlssFrameGenerationMaxMultiplier` | Read-only GPU/runtime limit; `0` means unavailable/unverified |
| `r_reflex` | `0` Off, `1` On, `2` On + Boost |

The controls are disabled automatically unless the Vulkan renderer and the
corresponding GPU/driver feature are available.

Frame Generation keeps its Off/On switch and has a separate 2x-6x slider.
2x requests one generated frame per rendered frame; 6x requests five. The slider
cannot select above the SDK-reported limit. A higher console/saved request is
capped at runtime without rewriting the saved preference. `nvidia_info` reports
requested, configured and maximum multipliers separately from presentation
counters. This is not an increase in the game's rendered FPS, nor a guarantee
that every present interpolates (focus, frame pacing and SDK status still matter).

### Multiplier verification (2026-09-13)

On the local RTX 5070 / Streamline 2.12.0 setup, the SDK reported a maximum of
6x. Isolated Q3DM6 tests used windowed 1280x720 DLAA/RR, fixed two samples and
NR off. These are functionality checks, not performance comparisons:

| Requested | Static / moving presentations per rendered-frame query | Result |
| --- | --- | --- |
| 2x | 2 / 2 | Passed follow-up with native Windows foreground verification; 120 presentations per 60 rendered-frame queries in each interval |
| 3x | 1 / 1 | Earlier test inconclusive: lacked reliable native foreground evidence; focused retest pending |
| 4x | 1 / 1 | Earlier test inconclusive: lacked reliable native foreground evidence; focused retest pending |
| 5x | 5 / 5 | Sustained requested multiplier in earlier test; native-foreground recheck pending |
| 6x | 6 / 6 | Sustained requested multiplier on initial and final builds; native-foreground recheck pending |

Correction: the initial range tests checked cached SDL focus, which was insufficient. A
follow-up debugger capture of the 2x case found the SDK declining interpolation
at its Windows foreground-ownership check, while another application actually
owned the foreground. SDK mode/count were correct, its startup cooldown had
expired, and the engine's old focus diagnostic incorrectly reported focused.
The initial 2x-4x failures were therefore inconclusive background-test results,
not proof that those multipliers were broken.

The user-attended 2x follow-up (`run-6eb1fcc21b104256baab6a73337c3bfc` under
the ignored `build-widescreen/mfg-menu-audit` directory) passed the current
counter checker. Both stationary and moving intervals sustained exactly 2x;
native unfocused/minimized/error counters stayed zero. The process exited
normally after 14,593 ms and saved-settings hashes were unchanged. This run used
the production NVIDIA runtime without the validation layer. No interpolation
algorithm change was needed. Native-foreground verification of the other
multipliers is still pending; this is not a full-range or validation-clean pass.

`nvidia_info` and the whole-interval focus counters now use Windows foreground
ownership. The checker rejects older logs without that evidence. This does not
force background interpolation or change the path tracer. Completed tests
exited normally in roughly 8-17 seconds and left saved settings unchanged.
Vulkan validation also reported presentation/layout/semaphore errors; neither
clean validation nor the older shutdown/restart issues are established fixed.

Reproduce with `tests/run-mfg-menu-check.ps1 -Multiplier 6 -Foreground`; this owns an isolated
home directory and an independent process guard (45 seconds by default).
`-Foreground` requests focus once for this test's own game window; Windows may
refuse it, in which case click the test window. It never repeatedly steals focus.
Keep the game in the foreground throughout the measurement. `-Developer 2`
adds verbose SDK logging, and `-RuntimeDirectory` allows an isolated SDK runtime
comparison without replacing the installed runtime DLLs.
`tests/menu_mfg_check.py --log <qconsole.log> --multiplier 6` checks real
static/moving presentation ratios, focus, SDK errors and completed teardown,
in addition to compiling the production request/cap policy and UI callbacks.
The runner's process exit code alone is **not** an interpolation pass. The
engine-options fixture covers hidden WIP persistence and slider staging/cancel.

Ray Reconstruction is a separate denoiser from Neural Rendering. When RR is
active, it replaces native temporal/spatial filtering and the normal DLSS color
evaluation; console-only Neural Rendering is bypassed.
The saved NR value is retained for use when RR is off. RR runs before exposure,
tone mapping, sharpening and the HUD. Enable **DLAA** in NVIDIA DLSS to retain
native rendering resolution; RR does not silently lower samples or bounces.

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

## Current lifecycle and diagnostics

Signed-runtime and export discovery happens at startup. The experimental NR
snippet initializes only when actually prepared; NR-off and RR-bypassed paths
do not initialize it or allocate its capability parameters. Successful first
use owns exactly one matching shutdown, including cleanup after later failures.
Runtime discovery is not proof of successful feature creation or evaluation.

Renderer teardown drains GPU work, releases feature resources/tags before their
images, and releases the separately initialized NR snippet before Streamline's
NGX core. The interposer stays alive through routed Vulkan resource destruction.
These ownership rules do not establish that all SDK cleanup or restart problems
are fixed.

The latest 2026-09-09 diagnostic captured NGX shutdown inside NVIDIA telemetry
cleanup, with a telemetry worker waiting while sending an event. The game
reached its 45-second guard. The cause of the worker wait remains unresolved;
no NVIDIA service, driver setting or proprietary DLL was changed. Recent RR
performance captures likewise timed out during shutdown and remain provisional.
See [current status](STATUS.md) and the
[dated diagnostic](archive/RAY_RECONSTRUCTION_DEVELOPMENT.md#rr-workgroup-experiment-2026-09-09).

`nvidia_info` reports requested/capable/active feature state, SDK results,
presentation counters, focus/minimized state and history-reset counters.
`pt_info` includes NVIDIA information alongside path-tracing state. A peak
presentation count of 1 is not generated-frame proof; a single peak above 1
is not proof of sustained interpolation. Verify with the game focused and
inspect real rendered frame time separately from presented frames.

The Vulkan backend honors archived, restart-applied `r_swapInterval`: 0
requests immediate presentation, 1 FIFO VSync. Requested/supported FG selects
immediate mode; the renderer explains that VSync applies when FG is off. If
immediate mode is unavailable, FG is reported unavailable without rewriting
the saved preference. See the pinned SDK's
[Vulkan VSync restriction](https://github.com/NVIDIA-RTX/Streamline/blob/v2.12.0/docs/ProgrammingGuideDLSS_G.md).

Historical FG-on private-resource validation errors, variable activation and
intermittent renderer-restart failures remain separate open work. Later
FG-off successes do not close them. Earlier evidence is preserved in the
[renderer lifetime audit](archive/PATH_TRACING_DEVELOPMENT.md#renderer-lifetime-hardening-2026-09-07),
[presentation audit](archive/PATH_TRACING_DEVELOPMENT.md#surface-routing-and-presentation-audit-2026-09-07)
and [bounded checkpoint](archive/PATH_TRACING_DEVELOPMENT.md#bounded-gpu-checkpoint-failed-shutdown-not-a-visual-pass).

Use [isolated testing](TESTING.md) and keep complete validation/exit evidence.
An enabled option, successful build or empty pre-shutdown log is not a
whole-renderer validation pass.
