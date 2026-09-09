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
tracked artifacts, candidate file sizes, common credential/key forms and the
embedded bytecode/C-array pairs. It includes untracked addable files, but does
not stage anything or scan Git history. Credential detection is deliberately
limited and never prints matching values; review the diff yourself as well.
External web links are not fetched or certified by this offline check.

Build trees, SDK/model downloads, game/mod PK3s, local settings, screenshots,
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

Current RR runs have an unresolved shutdown timeout. A completed rendering
script followed by watchdog termination remains diagnostic-only. Neither
increasing the timeout silently, discarding errors nor counting generated frames
turns it into an accepted performance improvement. See [current status](STATUS.md).

## Recorded cleanup verification — 2026-09-09

- Repository link/ignore/candidate checks and all 35 embedded binary/C-array
  pairs passed. No common high-confidence credential patterns were found; this
  is not a complete secret/history audit.
- All 34 ray/raster shaders regenerated into an ignored audit directory and
  matched byte-for-byte. The separate DLSS sharpening shader also matched when
  compiled with its original Vulkan 1.0 target; all SPIR-V validation passed.
  Shipping shader payloads were not changed by this documentation cleanup.
- The representative offline commands above passed, including actual transport
  math, RR failure cleanup, NR lazy initialization, terrain/layer composition,
  large texture allocation and engine options/VM precedence.
- The normal NVIDIA-enabled Windows release build completed successfully and
  reported its executable, renderer and modules up to date. This was not a
  fresh clean-room or newly repeated NVIDIA-disabled/platform build.
- No game/GPU tests ran in this cleanup pass. The existing runtime limitations
  remain open. The retired upstream Travis configuration/helper were removed;
  history and licensing notices were preserved. No commit or push was created.
