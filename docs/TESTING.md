# Testing and commit checks

Run from the repository root. The commands below describe the private validation
harness used by maintainers; the complete `tests/` directory is intentionally
ignored and is not shipped in source commits or release archives. No single
command certifies the whole renderer. Known runtime failures belong in
[current status](STATUS.md), not in an ignored log alone.

## Repository checks

For a distribution rather than a source-only commit, also follow the
[clean release packaging and dependency checks](RELEASING.md).

Python 3.10+ and Git are sufficient for the read-only hygiene checker when the
maintainer-only harness is present locally:

```text
python tests/repo_hygiene_check.py --shader-payloads
git diff --check
git diff --cached --check
git status --short
```

It checks project Markdown links/anchors, ignore-policy examples, accidental
tracked artifacts (including compiled/extracted QVMs), candidate file sizes,
documented numeric defaults, common credential/key forms and the embedded
bytecode/C-array pairs, including missing counterparts. It includes untracked addable files, but does
not stage anything or scan Git history. Credential detection is deliberately
limited and never prints matching values; review the diff yourself as well.
External web links are not fetched or certified by this offline check.

Build trees and scratch directories, SDK/model downloads, game/mod PK3s and
compiled QVMs, local settings, screenshots, profiling captures, runtime
binaries and the complete local `tests/` harness must remain ignored. Already
tracked upstream libraries under `code/libs` are preserved. GLSL, embedded
shader payloads and generated blue noise are source-build inputs and remain
tracked; test fixtures, scenarios and scripts stay in the maintainer's local
checkout rather than in the repository.

Review/stage logical groups rather than adding everything blindly. Confirm both
tracked and untracked source dependencies are included, inspect `git diff
--cached --stat` and `git diff --cached`, then write a commit message describing
implemented behavior, verification and remaining limitations. Repository cleanup
does not imply that a commit, push or release has been authorized.

## Representative offline regressions

Use a working GCC/G++ toolchain and the installed Vulkan SDK path. These checks
compile mocks/fixtures, not a running game. Examples from PowerShell:

```powershell
python tests/pt_rr_workgroup_check.py --cc gcc
python tests/pt_nr_lazy_check.py --cxx g++
python tests/pt_ray_reconstruction_check.py --cc gcc --sdk $env:VULKAN_SDK
python tests/pt_rr_lean_check.py --cxx g++ --sdk $env:VULKAN_SDK
python tests/pt_material_layers_check.py --cc gcc --cxx g++ --sdk $env:VULKAN_SDK
python tests/vk_texture_memory_check.py --cc gcc --sdk $env:VULKAN_SDK
python tests/vk_image_cache_check.py --cc gcc
python tests/release_defaults_check.py --cc gcc
python tests/pt_chrome_check.py
tests/run-engine-options-check.ps1 -Compiler gcc
tests/run-muzzle-flash-check.ps1 -Compiler gcc
tests/run-pt-push-debug-check.ps1 -Compiler gcc
python tests/nv_activation_check.py --cc gcc --sdk $env:VULKAN_SDK
g++ -std=c++17 -O2 -I code/renderer_vulkan/shaders tests/nv_look_fixture.cpp -o build-widescreen/nv-look-check.exe
./build-widescreen/nv-look-check.exe
```

Pass full compiler paths if they are not on PATH. The transport checker accepts
an optional `--baseline` when a preserved former integrator is available; such
local backup files are not required source-repository inputs. Do not claim the
optional baseline comparison ran when it was omitted. Asset-specific checks
such as hologram, panels and True Combat require the user's separately installed
game/mod data; never copy that data into Git to make the tests self-contained.
Consult each check's `--help` for its dependencies and opt-in GPU modes.
The release-default check also accepts `--profile` pointing to the config from
the isolated `run-release-smoke.ps1 -OfficialDefaults -Mode pt` run. It compares
registered values, all shader controls and the 3-sample/4-bounce/2× overrides.
Team Arena's source UI preserves existing sound preferences on first startup.
The performance runner and independent process guard accept explicit 1–120 second
limits. Allow time for setup and the complete scenario; a guard timeout is a
safety stop, not a successful lifecycle test.

The image-cache check compiles the production lookup with mocked image loading.
It covers clamp/repeat load order, collisions, reuse and the constant-white
exception. `--revision HEAD` can reproduce the failure while the pre-fix source
is still HEAD. The optional `pt_jumppad.cfg` scenario looks down at Q3DM6's stock
jump pad and captures a complete pulse cycle with post effects disabled. Run it
through the bounded performance harness in `-Synthetic` mode to preserve its
functional-test settings; its frame times are **not** benchmark evidence.
The corrected downward-view capture is in the ignored
`build-widescreen/rt-audit/performance-jumppad-clamp-view` (16 screenshots,
normal exit in 6.2 seconds, DLAA/RR, FG/validation off). The initial
`jumppad-clamp-before`/`after` cameras did not look far enough down and are not
visual-comparison evidence. The later captures show only the central expanding
pulse; the compiled old/new cache regression supplies the before/after fault check.

`run-engine-options-check.ps1` also exercises the production native Display-menu
callbacks and shared panel: FOV bounds/live application, VSync staging/cancel,
explicit/deferred restart, and cross-game profile persistence. The optional
`run-postfx-check.ps1 -OptionsLayout -VSync 0` (or `1`) captures both Display menus
and a 90→110→90 FOV sequence. Inspect captures and the selected present-mode log,
not merely the requested VSync value; keep Frame Generation off for this check.

Software tracing and optional denoising have separate **offscreen GPU** checks
in [the software guide](RTX.md#verification) and
[denoising guide](SOFTWARE_DENOISING.md#verification). They use actual devices;
do not describe them as CPU-only checks or run them unintentionally during a
documentation-only pass.

## Embedded shaders

The normal build links checked-in C arrays under
`code/renderer_vulkan/shaders/Compiled`; editing GLSL does not regenerate them.
With a Vulkan SDK and GCC on PATH, rebuild ray/compute payloads from PowerShell:

```powershell
code/renderer_vulkan/shaders/compile-raytracing.ps1 -VulkanSDK $env:VULKAN_SDK
```

`-Raster` also regenerates raster vertex/fragment shaders. `-Shaders` restricts
the compute shader list; omit it for all supported ray/compute variants.
The separate ordinary-DLSS sharpening shader is outside that ray-shader default
list. Its checked-in payload currently uses Vulkan 1.0 without the optimization
pass. To reproduce it exactly, compile `shaders/dlss_sharpen.comp` with
`glslangValidator --target-env vulkan1.0 -V`, validate with `spirv-val`, and use
the same `bintoc` converter. Requesting `-Shaders dlss_sharpen` from the ray
generator instead uses Vulkan 1.2 plus optimization and intentionally changes
the payload; a hash difference from that command alone is not stale GLSL.
The script compiles, optimizes, validates SPIR-V and regenerates matching C
arrays. Commit the changed GLSL and matching payloads together. The converter
executable is disposable/ignored; generated payloads are intentionally tracked.

For a non-mutating source/payload comparison, choose a new ignored output folder:

```powershell
code/renderer_vulkan/shaders/compile-raytracing.ps1 -VulkanSDK $env:VULKAN_SDK -OutputDirectory build-widescreen/shader-audit
```

Compare regenerated payloads with `shaders/Compiled` using the same toolchain.
Compiler versions or debug metadata can change bytes: investigate differences,
do not overwrite known source changes merely to make hashes match. The hygiene
check verifies binary-versus-C-array consistency, not GLSL equivalence; actual
regeneration and SPIR-V validation cover the latter build contract.

Build the release executable/renderer afterward using the [normal build](../README.md#building-on-windows-x64).
Use `USE_NVIDIA_DLSS=0` for a separate SDK-free build. Do not mix old renderer
DLLs with an executable built against a different renderer export interface.

## Local post-effect checks

### Door motion and occlusion regression

`python tests/pt_entity_flags_check.py --cc <gcc>` compiles the actual
entity-to-ray visibility conversion. It verifies that BSP brushes suppress
legacy stencil shadows without becoming transparent to shadow rays, while
non-brush flags and weapon tagging remain unchanged.
`python tests/pt_brush_motion_check.py --cc <gcc>` compiles the real capture
function and exercises reordered/split brush batches, translation, rotation,
teleport/generation changes, missing tracking and the deformed/mesh fallback.
Both run normal and fast-math variants; `--source <vk_pathtrace.c>` on the
motion check can demonstrate failure against a saved pre-fix source.

For the Q3DM0 double door, run `tests/run-postfx-check.ps1 -Doors -Mode pt
-PathTracingScale 0.5 -NoValidation -VulkanSDK <sdk>` and then
`python tests/pt_doors_image_check.py <run>/home/baseq3/screenshots --opening`.
The fixture uses fixed 16 ms steps and a known 90-degree horizontal projection;
its decoded motion is compared with an independent camera-rotation calculation,
not with another renderer-generated motion estimate. Stationary, panning and
door-opening captures are included. `-FrameGeneration 1` enables 2x FG for a
functional rerun (not a performance benchmark). Inspect FG status in the log:
engine screenshots contain rendered frames, not the synthesized presentation
frames, so they cannot establish final FG interpolation quality.

2026-09-13 closed-door evidence (DLAA/RR 480x270 to 960x540):

| Run | Result |
| --- | --- |
| Before, `run-a89aafdca38f46d19231b83391fa6123` | Closed-door pan motion error averaged 24.834 output pixels; opposing vectors reproduced |
| After, `run-286c7976d9ed4a4d8de49231453d2dd8` | Mean 0.102, maximum 0.213 output pixels; normal exit in 6,641 ms |
| 2x FG, `run-a1f4597aeccf4f95aa58397bf034ca9e` | Same motion result; FG active with peak two presented frames and no failed queries; normal exit in 6,640 ms |
| Opening check, `run-0ceb17e2100e40c59f31d92cbf8d6c78` | Closed-door motion and open-door depth checks passed; normal exit in 6,782 ms; FG requested but suspended while unfocused (peak one frame), not additional generated-frame evidence |

These runs used isolated settings and validation off. The pre-fix capture
function also failed the reorder fixture; the corrected function passed both
build modes. The initial approach phase kept noclip enabled and did not open
the door; it supports closed-door claims only. The revised fixture temporarily
disables noclip at the trigger and checks that opening reveals deeper geometry.
Both SDK-enabled and SDK-free release builds passed. No RR/FG algorithm,
render scale, sampling default or saved user setting was changed by these fixes.
The light-occlusion correction is verified at flag conversion; these captures
are not an isolated light-source/shadow-energy benchmark.

### Package checks

Build the example packages first; the external effect loader does not use the
embedded-shader arrays above:

```powershell
tools/compile-postfx.ps1 -VulkanSDK $env:VULKAN_SDK
gcc -O3 -ffast-math tests/postfx_parse_fixture.c -o build-widescreen/postfx-parse.exe
./build-widescreen/postfx-parse.exe
gcc -O3 -ffast-math tests/postfx_spirv_fixture.c -o build-widescreen/postfx-spirv.exe
$fx = 'build-widescreen/release-mingw64-x86_64/postfx'
./build-widescreen/postfx-spirv.exe "$fx/colorgrade.comp.spv" "$fx/fullscreen.vert.spv" "$fx/vignette.frag.spv" "$fx/chromatic.frag.spv" "$fx/lensdistortion.frag.spv" "$fx/lensdirt_prefilter.frag.spv" "$fx/lensdirt_blur_h.frag.spv" "$fx/lensdirt_blur_v.frag.spv" "$fx/lensdirt.frag.spv" "$fx/bokehdof.frag.spv" "$fx/tonemap.frag.spv" "$fx/filmgrain.frag.spv"
gcc -O3 -ffast-math tests/postfx_packages_fixture.c -o build-widescreen/postfx-packages.exe
./build-widescreen/postfx-packages.exe postfx
gcc -O3 -ffast-math tests/postfx_motion_fixture.c -o build-widescreen/postfx-motion.exe
./build-widescreen/postfx-motion.exe
gcc -O2 tests/postfx_png_fixture.c code/renderer_vulkan/R_ImagePNG.c code/qcommon/puff.c -o build-widescreen/postfx-png.exe
./build-widescreen/postfx-png.exe postfx/lensdirt.png
tests/run-engine-options-check.ps1 -Compiler gcc
```

Also pass `"$fx/motionblur.frag.spv"` as a fragment argument to the interface
fixture. It checks the version-5 motion binding and 128-byte push layout while
rejecting those resources in older package versions.
Include `"$fx/bloom_prefilter.frag.spv"`, `"$fx/bloom_blur_h.frag.spv"`,
`"$fx/bloom_blur_v.frag.spv"` and `"$fx/bloom.frag.spv"` for the Bloom package.
The engine-options fixture checks startup bloom migration in normal and
fast-math builds: missing versus explicit new preferences, archived values,
master-switch preservation, no transient-debug import and invalid-value rejection.

Use `tests/run-postfx-check.ps1 -Bloom` (plus the desired mode/runtime/SDK flags)
for a deterministic bright-shape chart, intensity/threshold/radius/debug checks,
ordering, shared quarter targets, a scene preview and the Effects listing.
Run `python tests/postfx_bloom_image_check.py <run>/home/baseq3/screenshots`
afterward; optional `--preview <output.png>` creates a review sheet. The checker
uses analytic chart pixels and an independent dense Gaussian convolution,
including float16 intermediates. It verifies zero bypass, normal and maximum
radius, no-blur extraction, stable output, effect ordering, shared v3 targets
and unmodified ammo digits. Rotating HUD icons and decaying health digits are
excluded from the pixel-identity assertion. `-Bloom` cannot be combined with
the other scenario switches. The RR-debug scenario now toggles the package's
enable/debug cvars rather than the retired built-in controls.

These are offline checks. Also build with and without `USE_NVIDIA_DLSS` after
engine/renderer interface changes. The optional attended GPU scenario is:

```powershell
tests/run-postfx-check.ps1 -Mode raster -Foreground -VulkanSDK $env:VULKAN_SDK
```

`-Mode pt` uses DLAA/RR at fixed two samples, FG off. Both modes use 960x540
functional-test settings, not a performance profile. `-RuntimeDirectory` can
select an SDK-free build for raster testing; game data and example effects
still come from the normal release directory. The test installs a mixed-pass
fixture only in its isolated home. Inspect all five screenshots and the raw
validation log; package readiness and scenario completion alone do not prove
correct pixels. A guard timeout remains a lifecycle failure. See the
[package guide](POST_PROCESSING.md#checks) for scope and format limitations.
After inspecting the images, run
`python tests/postfx_image_check.py <screenshots-directory>` (Pillow required)
to check scene desaturation, vignette darkening, mixed-pass color and HUD
exclusion. These image assertions do not certify lifecycle or Vulkan validation.

Add `-LensEffects` to install isolated deterministic charts/depth probes and capture
lens/tone effects, bokeh DOF and static/animated film grain. Fixtures are never installed in the user's
package directory. Check those captures with:

```powershell
python tests/postfx_lens_image_check.py <screenshots-directory> --texture postfx/lensdirt.png --preview build-widescreen/postfx-audit/lens-preview.png
python tests/postfx_grain_image_check.py <screenshots-directory> --preview build-widescreen/postfx-audit/grain-preview.png
python tests/postfx_optics_image_check.py <screenshots-directory> --preview build-widescreen/postfx-audit/optics-preview.png
```

This requires Pillow and NumPy. It checks neutral identity, RGB separation,
both warps/black borders, static highlight-driven dirt, tone controls and HUD
exclusion. `--texture` independently reconstructs the expected dirt pixels from
the supplied PNG, including aspect crop, dense Gaussian blur and glare. The PNG fixture
also checks missing/truncated/oversized files, allocation ownership and that
large decoded buffers do not enter the engine's fixed zone.
The grain check compares real shader pixels against an independent CPU hash,
checks zero-amount identity, stationary grain, one/four-pixel cells with equal
amplitude, animated seed changes and opaque HUD exclusion. Translucent glyph
edges legitimately reveal the noisy scene underneath and are excluded.

The optics check compares real GPU pixels against independent dense Gaussian
convolution and a depth/disk gather. Its isolated light shapes distinguish the
old nine-copy gather from continuous scattering at normal and maximum spread.
An extra identity compute pass flips the intermediate target parity to catch
overwritten original scene color. Packed real scene depth drives CPU predictions
for manual focus, center autofocus, round/hexagonal bokeh and highlight gain;
zero aperture/radius must preserve the source. Real-scene off/on captures are
also provided for inspection. The parser fixture checks v3 target routing and
rejects incompatible depth/downsample declarations; the SPIR-V fixture rejects
original/depth bindings in older interfaces. Its first two arguments must be
compute and vertex modules; subsequent arguments must be fragment modules.

Use `-Mode pt -PathTracingScale 0.5 -NoValidation -LensEffects` to exercise
lower-resolution tracer depth behind full-output RR color. The default scale is
1. These are functional image checks, not saved-quality performance benchmarks
or tests of moving foreground edges, focus-rack stability or FG-on effects.

### Motion blur

```powershell
tests/run-postfx-check.ps1 -MotionBlur -Mode raster -Foreground -VulkanSDK $env:VULKAN_SDK
tests/run-postfx-check.ps1 -MotionBlur -Mode pt -PathTracingScale 0.5 -NoValidation -Foreground -VulkanSDK $env:VULKAN_SDK
python tests/postfx_motion_image_check.py <screenshots-directory> --preview build-widescreen/postfx-audit/motion-preview.png
```

Select an SDK-free runtime for independent raster validation. This scenario
records a fixed grid, actual camera-motion/depth guides, stationary and turning
frames, shutter/cap/zero controls, stop recovery and real-scene/menu captures.
Noclip fixes the test camera against gravity/jump pads; logged stationary view
positions must agree. The CPU checker accounts for the RG8 probe's velocity
quantization and different surfaces exposed by consecutive moving frames.
Run the matrix fixture under normal and fast-math builds as well. None of these
checks claims broad per-object/weapon/transparent-surface or FG-on validation.
Do not combine `-MotionBlur` with the other scenario switches.

### Options layout and controls

For the six-tab menu layout, run:

```powershell
tests/run-postfx-check.ps1 -OptionsLayout -Mode pt -NoValidation -VulkanSDK $env:VULKAN_SDK
```

This captures Exposure, Lighting without its old exposure row, collapsed Effects,
and the classic graphics-menu shortcut. Inspect the four images for clipping
and overlap. `tests/run-engine-options-check.ps1` checks the actual menu code's
mouse/keyboard folding, independent enable state, scrolling, hidden pending
Apply/Cancel, logarithmic exposure ranges, staged reset and shared-profile
round-trip in both normal and fast-math builds. The GPU scenario uses isolated
settings and is a layout check, not a performance test.

### RR output isolation

`tests/run-postfx-check.ps1 -RRDebug -Mode pt -NoValidation -VulkanSDK $env:VULKAN_SDK`
runs the dedicated RR-only routing scenario. Use `-Mode raster` and an SDK-free
`-RuntimeDirectory` to check safe fallback with Vulkan validation. Do not combine
`-RRDebug` and `-LensEffects`. Both use the existing short owned-process guard,
isolated settings and FG off, and require a matching current engine/renderer.

```powershell
python tests/pt_rr_debug_check.py <screenshots-directory> --mode pt --preview build-widescreen/postfx-audit/rr-debug-preview.png
```

The deliberately obvious chart post-effect must disappear only on successful
PT/RR debug frames, alongside HUD digits. Forced bloom-debug/NV/sharpness must
not replace that result. Captures then verify console/options visibility,
resumption, toggle-off restoration and the main menu after disconnect. Raster
must keep normal presentation instead of showing a stale or blank RR image.
Inspect captures and logs in addition to running the checker. Small temporal
differences between live PT frames are allowed; this is not a timing benchmark,
RR-failure injection test or FG-on transition check.

## Bounded game comparisons

Game/GPU tests are explicit, attended opt-ins, not part of the hygiene checker.
Close an existing game before a test; never replace its DLL or terminate a
user-started process. Use PowerShell 7 and build the independent process guard
as described in [GPU profiling](GPU_PROFILING.md#prepare-the-build).

For example, this repeats the current workgroup baseline with the saved profile,
explicit DLAA/two-sample/RR settings and a 45-second owned-child deadline:

```powershell
tests/run-pt-performance.ps1 -Label rr-rows-unique-label -Config pt_rr_workgroup.cfg -EnablePathTracing -BenchmarkDlss 5 -BenchmarkRayReconstruction 1 -BenchmarkSamples 2 -BenchmarkAdaptive 0 -BenchmarkNeuralRendering 0 -BenchmarkRRRows 128 -Bounded -TimeoutSeconds 45 -VisibleWindow
```

The performance runner reads per-game **and shared** presentation settings,
records overrides/hashes, and forces Frame Generation off without changing the
user's saved files. It retains the saved bounce count and remaining quality.
Use unique labels; never reuse old screenshots after a failed run. The legacy
`run-pt-regression.ps1` uses synthetic settings and is not the saved-settings
performance harness. Do not launch it expecting a matched performance result.

Require matching resolution, samples, bounces, filtering, exposure, installed
texture pack, camera and executable/renderer (except the intended A/B change).
Confirm actual feature evaluation, enough settled measurements, image checks,
unchanged saved settings, a completion marker and clean exit. Validate the
retained variant separately with `-Validation`; profiler/validation overhead is
not a normal FPS result. Never measure while offline texture inference or
another heavy GPU workload is running.

Some recorded RR runs have unresolved shutdown timeouts; the attended 2x FG
follow-up exited normally, but does not establish a general lifecycle fix. A
completed rendering script followed by watchdog termination remains diagnostic-only. Neither
increasing the timeout silently, discarding errors nor counting generated frames
turns it into an accepted performance improvement. See [current status](STATUS.md).

`run-software-lighting.ps1` uses a default 45-second owned-process limit,
configurable from 15–90 seconds. Its Urban Terror scenario
(`-Game Q3UT3 -Scenario rt_urban_nv.cfg`) exercises the real mod's goggle item.
It requires a normal exit by default. `-AllowShutdownTimeout` explicitly allows
capture-only NV testing after the scenario completes, with a warning; it does
not convert the known NVIDIA shutdown failure into a lifecycle pass. Existing
game settings are hash-checked. FG/NR are off in these rendering checks; they
are not performance benchmarks. See [NV evidence](archive/NIGHT_VISION_DEVELOPMENT.md).

## Frame Generation menu and presentation checks

These offline checks do not launch a game or use the GPU:

```powershell
python tests/menu_mfg_check.py --compiler C:/msys64/ucrt64/bin/g++.exe
python tests/pt_frame_generation_check.py --self-test
tests/run-engine-options-check.ps1 -Compiler C:/msys64/ucrt64/bin/gcc.exe
```

The menu checker requires the fetched Streamline 2.12.0 headers under
`build-widescreen/deps/streamline-sdk-v2.12.0/include`. It compiles production
menu callbacks and the SDK request/capability policy with mocked device/API
results. The engine-options fixture covers preservation of hidden software/NR
settings and multiplier staging/cancel. These passes do not prove that the SDK
generates frames on a real device.

For an attended functionality test, close any existing game first and use a
complete NVIDIA-enabled release installation with its runtime DLLs and game
data in `build-widescreen/release-mingw64-x86_64`. The runner currently expects
the validation layer in `C:/VulkanSDK/1.4.350.0/Bin`; check that local prerequisite
before a validation run. `-NoValidation` selects a functionality-only run:

```powershell
tests/run-mfg-menu-check.ps1 -Multiplier 2 -Foreground -NoValidation
```

The scenario uses windowed 1280x720 Q3DM6, DLAA/RR, native tracing scale, two fixed
samples, NR off and FG on, followed by menu checks and exit. These are explicit
functional settings, not the saved-settings performance benchmark above. Each
run creates a unique ignored `build-widescreen/mfg-menu-audit/run-...` directory
with an isolated settings home, console/guard logs and captures. The runner
hash-checks existing user `baseq3/q3config.cfg` and shared `vq3e-rendering.cfg`.
Its independent owned-process deadline defaults to 45 seconds and accepts
15-90 seconds via `-TimeLimitSeconds`; a timeout is not a clean-exit pass.

`-Foreground` makes one focus request for this test's own game window. Windows
may refuse it; click the window if needed and keep it visible and focused
throughout measurement. It does not repeatedly reclaim focus. `-Developer 2`
enables verbose SDK diagnostics. `-RuntimeDirectory` selects a complete isolated
runtime installation for SDK comparisons without replacing installed DLLs;
the normal release directory remains the game's base-data path.

After the runner prints its evidence directory, check that run's
`home/baseq3/qconsole.log` with:

```text
python tests/menu_mfg_check.py --log <qconsole.log> --multiplier 2
```

Replace the placeholder with the actual quoted log path. For another supported
multiplier, change both arguments together. The checker requires the configured
multiplier to be sustained during both stationary and moving intervals, native
Windows foreground evidence, no new focus/minimization/SDK failures during
measurement, scenario completion and SDK teardown. Older SDL-only focus logs
are inconclusive. A successful launcher exit, an enabled cvar or one generated
frame is insufficient. Inspect the captured menus separately, including both
the stock Graphics Options menu and the engine-owned panel. Omit
`-NoValidation` for a separate validation run and review its errors explicitly;
the counter checker is not a Vulkan-validation or image-quality certificate.

The 2026-09-13 attended 2x follow-up passed both intervals and exited normally
in 14.6 seconds with saved settings unchanged. Native-foreground verification
of 3x-6x remains pending. Existing NVIDIA presentation-validation and older
restart/shutdown failures remain open. Keep these limits with the
[recorded results](DLSS.md#multiplier-verification-2026-09-13); generated
presentations must not be reported as a rendered-FPS improvement.

## Recorded cleanup verification — 2026-09-13

- Repository link/anchor, ignore-policy, candidate size/credential and all
  **51** embedded binary/C-array pair checks passed. This is a limited
  working-tree check, not a full secret or Git-history audit.
- All 50 default ray/raster shaders were regenerated into an ignored audit
  directory, SPIR-V validated and matched byte-for-byte. The separate DLSS
  sharpening shader also matched with its original Vulkan 1.0 target.
  No shipped shader payload was changed by this cleanup.
- Windows x64 release builds passed with `USE_NVIDIA_DLSS=1` and `0`, including
  native/QVM base-game and Team Arena modules. The SDK-free build exposed and
  fixed a stale `vk_sl_configure` fallback signature (missing the scale argument).
  These were local builds, not fresh-clone, Linux or macOS verification.
- Engine options/VM precedence, weapon scale controls, push-log handle lifetime,
  filesystem write-error reporting, material layers/portal tone mapping, NV
  activation/appearance math, RR workgroup coverage and RR resource failure
  handling passed offline. The outdated RR mock gained the current lighting-mode
  and exposure fields plus assertions for guide selection and exposure forwarding.
  All 22 tracked/addable PowerShell scripts parsed successfully.
- Two compiled QVMs accidentally tracked under `.build-tmp/nvg-inspect/vm`
  were removed from the working tree and backed up under ignored
  `build-widescreen/repo-cleanup-8216e1be0c8a48398ae442d2c2606505`.
  They already exist in commit `0559f4f`; ordinary deletion does **not** purge
  published Git history. No history rewrite or force-push was performed.
- Dated profiler/experiment reports moved to the archive. Software/NV test
  narratives were separated from current guides; links and stale claims were
  corrected. Required shader inputs, tests, upstream build support and licensing
  notices were retained. Ignored game assets, rollback backups and local builds
  were not deleted.
- No game/GPU tests ran during cleanup. NVIDIA shutdown, vendor motion-format
  warnings and RTX descriptor-lifetime errors remain documented in
  [current status](STATUS.md). No commit or push was created.
