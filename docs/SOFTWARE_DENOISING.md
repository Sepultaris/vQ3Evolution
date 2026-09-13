# Software-mode denoising

These controls affect **`r_rayTracing 1` only**. They do not select denoisers or
change reconstruction settings for mode 2's hardware tracing or NVIDIA RR.
Neither change increases the number of traced samples. The existing default
remains two samples per pixel.

## Native local history (default)

```text
/set r_softwareRayTracingHistory 1
```

Previously, any changed dynamic geometry capped diffuse history at four frames
across the entire screen. Software mode now applies that cap to object pixels
instead. Static surfaces may accumulate up to `r_pathTracingHistory` (default
8), subject to the existing reprojection, surface compatibility, clipping and
lighting-change tests. This is an accumulation limit, not a promise that every
pixel retains eight frames. Moving shadows still depend on radiance anti-lag;
there is no new previous-frame visibility trace.

Set `r_softwareRayTracingHistory 0` for the old global-cap behavior. This changes
live. The native filter is still available without any NVIDIA SDK or runtime.

## Optional NRD RELAX (local evaluation)

NRD is a conventional compute denoiser, **not DLSS Ray Reconstruction or a
neural network**. Its RELAX diffuse/specular path runs without hardware ray
tracing or tensor-core extensions. The local Windows adapter is optional and
defaults off:

```text
/set r_rayTracing 1
/set r_softwareRayTracingDenoiser 1
/vid_restart
```

The log must report `Software NRD RELAX active`. A missing/incompatible DLL,
unsupported GPU requirement or initialization failure reports the reason and
retains native reconstruction. To return to native:

```text
/set r_softwareRayTracingDenoiser 0
/vid_restart
```

The adapter receives separate unfiltered diffuse/specular radiance, material
factors, shading normals/roughness, linear view depth, world-space object motion,
unjittered camera matrices, jitter and first-continuation hit distances. It
filters lighting before restoring material color and applying exposure. Local
light-change probes lower history confidence. Camera/history resets and gaps
caused by debug views or disabled temporal reconstruction restart accumulation.

Glass, water, reactive layers and optical transmission retain the native/raw
fallback paths rather than being treated as opaque surfaces. Native temporal
reconstruction still runs for these signals; NRD replaces the three spatial
filter dispatches. It adds conversion work, storage and its own history/filter
passes. **It is not a measured FPS optimization**, and may cost more on a given
GPU. Its purpose here is smoother low-sample opaque lighting.

The first-continuation distance is an approximation for mixed diffuse/specular
paths. Complex moving reflections, optical boundaries and arbitrary mod effects
need broader visual testing. There is no claim of DLSS RR-equivalent quality.

## Local build and licensing

The pinned SDK is [NVIDIA NRD](https://github.com/NVIDIA-RTX/NRD), revision
`bf877181058988ec5785f82c4189be0be75c1902` (4.18.0). Its current `LICENSE.txt`
contains the **NVIDIA RTX SDK license**. It is source-available, not a permissive
open-source dependency. Review that license before downloading/using it.
Compatibility and redistribution alongside this GPL project have **not** been
cleared. A separate DLL does not by itself resolve those licensing questions.
Do not publish the SDK, generated SDK shaders, adapter binary or combined build
as part of this experiment.

Only engine-owned adapter/conversion source is added to this repository. The
SDK checkout and all SDK-derived outputs stay under ignored `build-widescreen`.
The regular engine build has no NRD SDK/link dependency and still works without
the adapter. The adapter build/loader currently targets Windows x64; Linux/macOS
integration has not been implemented or tested.

After reviewing the license, place the pinned checkout in
`build-widescreen/nrd-source`, then build with CMake, Visual Studio C++ tools and
the Vulkan SDK (including DXC):

```powershell
./tools/build-software-nrd.ps1 -VulkanSDK C:/VulkanSDK/1.4.350.0
```

This produces `build-widescreen/nrd-adapter/Release/vq3e_nrd.dll`. For a local
test, put it beside `vQ3Evolution.exe`, retaining the SDK's license notice.
The loader uses that exact executable-relative path, not a mod/PK3/search-path
DLL. The SDK build uses embedded SPIR-V, no NRI, no DXBC/DXIL and no quad
intrinsics, avoiding a compute-derivative-extension requirement.

Beyond the software tracer's existing requirements, this adapter checks core
storage-image read/write-without-format features, ten per-stage storage images,
47 storage buffers and 564 total per-stage resources. Its textures must support
the requested sampled/storage formats. Non-RTX branding does not guarantee that
a GPU satisfies these requirements or has adequate performance/memory.

## Verification

```powershell
./tests/check-rt-software.ps1 -VulkanSDK C:/VulkanSDK/1.4.350.0 -DeviceIndices 0,1
./tests/check-software-denoise.ps1 -VulkanSDK C:/VulkanSDK/1.4.350.0 -DeviceIndices 0,1 -Validation
./tests/run-software-lighting.ps1 -Denoiser 1 -LocalHistory 1 -Scenario rt_software_denoise.cfg -Validation
```

The denoising fixture runs offscreen with **zero device extensions**, on the
actual temporal shader and adapter. It checks legacy/static/object history
counts, surface mismatch, changed-light rejection, two-sample synthetic noise,
texture preservation and response to a lighting step. On 2026-09-12 it passed on
both the NVIDIA GeForce RTX 5070 and AMD Radeon integrated GPU. The synthetic
128x96 fixture's quality/timing numbers are not game benchmarks.

Windowed Q3DM6 tests use isolated 960x540, two samples, two bounces, no DLSS,
Frame Generation or Neural Rendering, and a 45-second process guard. They check
actual NRD activation, normal exit and Vulkan synchronization validation. The
fixed camera/turn sequence captures both stationary and newly exposed surfaces.
This is scoped image-quality/functional coverage, not an older-GPU FPS claim.
During the original denoiser integration, the pre-existing hardware payloads
were checked against its local pre-change backup and matched. That dated check
does not mean later shared material or post-processing changes left them unchanged.

Recorded local results on 2026-09-12:

- Native local-history Q3DM6 run: normal exit, 24.0 seconds total process time,
  empty validation log. The NRD comparison visibly smooths the floor lighting;
  darkness and reflective/transparent pickups remain separate quality issues.
- Missing-adapter run: explicit native fallback, normal exit in 23.0 seconds,
  empty validation log. This is not counted as an NRD pass.
- Final NRD Q3DM6 run: actual RELAX activation, stationary/turn captures,
  normal exit in 38.7 seconds and an empty synchronization-validation log.
- Both offscreen devices passed synchronization validation. The full textured
  game has not been benchmarked on the integrated AMD GPU.
- These process times include startup/shutdown and are **not frame timings**.
  No rendered-FPS improvement is claimed.
