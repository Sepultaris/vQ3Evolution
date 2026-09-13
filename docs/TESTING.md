# Testing and commit checks

Run from the repository root. The commands below separate offline source checks,
build validation and opt-in game/GPU tests. No single command certifies the whole
renderer. Known runtime failures belong in [current status](STATUS.md), not in
an ignored log alone.

## Repository checks

Python 3.10+ and Git are sufficient for the read-only hygiene checker:

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
compiled QVMs, local settings, screenshots,
profiling captures and runtime binaries must remain ignored. Already tracked
upstream libraries under `code/libs` are preserved. GLSL, embedded shader
payloads, generated blue noise, regression fixtures, test scenarios and scripts
are source-build inputs and should not be removed as old builds.

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
The performance runner and independent process guard accept explicit 1–120 second
limits. Allow time for setup and the complete scenario; a guard timeout is a
safety stop, not a successful lifecycle test.

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
