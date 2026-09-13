# Software path tracing (mode 1)

`r_rayTracing 1` shades the world with a software ray-tracing compute kernel.
It replaces baked lightmap lighting, not just the shadow term. Triangle
intersections and BVH traversal execute in ordinary GPU compute; no hardware
RT extensions, acceleration structures, buffer device addresses or NVIDIA
runtime DLLs are required.

Mode 1 shares the material compiler, lighting integrator and native temporal/
spatial reconstruction with the existing path tracer. It traces primary
visibility, local/emissive/sun lighting, secondary bounces, reflections and
transmission, including authored alpha tests, terrain blending, animated
materials, decals and fog. Lightmap stages and baked world vertex RGB are
excluded from transport. The compute kernels do not sample raster scene color
or depth to obtain lighting. UI and HUD presentation remain rasterized.

`r_rayTracing 2` uses the separate hardware backend and NVIDIA Ray Reconstruction.
Shared submission supplies material/geometry inputs to both modes. Later shared
material fixes can affect both tracers; historical byte-for-byte mode-2 checks
apply only to the specific software-tracing changes recorded in the archive.

## Use

```text
/cl_renderer vulkan
/r_rayTracing 1
/vid_restart
```

Mode 1 always uses software traversal, including on RTX cards. Use mode 0
for rasterization or mode 2 for hardware path tracing, followed by a restart.

| Control | Meaning |
| --- | --- |
| `r_pathTracingSamples` | Fixed samples per pixel (existing default: 2) |
| `r_pathTracingBounces` | Maximum transport interactions |
| `r_pathTracingScale` | Internal scene resolution fraction; restart required |
| `r_pathTracingDenoise`, `r_pathTracingTemporal` | GPU-independent reconstruction |
| `r_softwareRayTracingHistory` | Default 1: local object history cap, rather than limiting every static wall when an object moves |
| `r_softwareRayTracingDenoiser` | Default 0: native; 1: optional locally built NRD RELAX adapter, restart required |
| `r_pathTracingExposure`, `r_pathTracingAutoExposure` | Manual/automatic exposure |
| `r_pathTracingAmbient` | Optional ambient fill |
| `r_pathTracingSunAngle`, `r_pathTracingLightRadius` | Sun/local-light penumbra |

See [PATH_TRACING.md](PATH_TRACING.md) for shared material/lighting controls.
See [SOFTWARE_DENOISING.md](SOFTWARE_DENOISING.md) for the new mode-1 history
policy, optional denoiser, local build instructions and licensing limitations.
The old shadow-mask `r_rayTracingShadowStrength` and
`r_rayTracingShadowBias` settings no longer affect mode 1.
RR-only adaptive sampling, light reuse, hardware shader-profile and specialized
RTX pipeline controls do not select hardware kernels in software mode.

## Requirements and limitations

A Vulkan 1.2 graphics/compute device with sampled-image nonuniform indexing is
required, along with capacity for 514 sampled-image/sampler descriptors,
46 storage-buffer descriptors, three storage images and 563 total stage
resources. Unsupported devices retain raster rendering and report the missing
requirements. Lack of RTX branding alone does not establish compatibility.

This is GPU software traversal, not a CPU renderer. Full light transport costs
more than the former shadow mask. There is no older-GPU FPS guarantee; resolution,
samples, bounces, material complexity and GPU memory still matter. Native
reconstruction does not require DLSS or an NVIDIA GPU. Mode 2 remains the route
to NVIDIA Ray Reconstruction.

Material/portal limitations of the shared transport still apply; sharing it does
not claim that every mod effect is fully supported. Subsequent work adds
[planar BSP camera portals](PATH_TRACING.md#camera-portals) to both tracers.
First-person geometry is traced with its own
visibility mask; it is not a baked-lighting color copy.

## Implementation

- `vk_raytracing.c` and `rt_software_bvh.h`: static world plus per-frame dynamic
  binned-SAH BVHs, CPU escape-link construction and synchronized GPU uploads.
- `shaders/pt_software_query.glsl`: resumable candidate traversal with triangle
  IDs, barycentrics, ray masks, minimum/maximum distances and rejected candidates.
  Direction-ordered escape links avoid retaining a per-ray traversal stack during
  material callbacks. Both trees use the same global triangle/material IDs.
  Box tests reuse reciprocal ray directions within each query resume, with
  conservative box-bound rounding and the original scalar test for zero or
  near-parallel directions. Triangle distances and material acceptance are unchanged.
  The tree header includes the static world's visibility mask. Weapon-only and
  decal-only queries start at the dynamic tree when that mask excludes the static
  world; mixed/world masks still search the static tree. No extra buffer is needed.
- `rt_software_order.h`: eight escape orders selected by ray direction signs.
  The dominant child-center separation axis chooses the likely near side first;
  this is an ordering heuristic, not an exact near-distance sort. Every permitted
  child remains reachable. Static orders are built/uploaded with the map; dynamic
  orders follow each dynamic-tree rebuild. Packed direction bits reuse existing
  query flags instead of adding a query stack or extra persistent query fields.
- `shaders/pt_software.comp` and `pt_software_guides.comp`: separately compiled
  software variants of the shared integrator; no ray-query instructions.
- `vk_pathtrace.c`: mode-1 storage descriptors and software pipeline selection;
  mode-2 descriptor layout, kernels and hardware dispatch remain selected as before.

Generated SPIR-V and embedded C payloads are source-controlled. The renderer
contains the feature; no replacement PK3 is needed.

## Software-mode profiling

`r_pathTracingProfile 1` now produces `SW_PROFILE` records in mode 1. The old
eight-timestamp report could not complete because software scene recording
never emitted its upload-boundary timestamps. Hardware mode keeps its original
eight-query report; software mode uses thirteen timestamps to separate tracing,
native temporal filtering, NRD and final upscaling.

All duration fields are milliseconds. `gpu` spans the recorded GPU frame;
`wall` is the instrumented interval between frame beginnings, not a generated
frame rate. `scene_cpu` covers software BVH construction, traversal-link updates
and upload-command preparation, not the entire game's CPU time. `upload_bytes`
counts scene-geometry/traversal uploads. Results are read after the existing
render fence, with no new GPU wait; incomplete query sets are discarded.
Turn profiling off with `r_pathTracingProfile 0` after measuring.

To reproduce a bounded comparison with the user's saved graphics settings:

```powershell
./tests/run-software-lighting.ps1 -UseSavedSettings -Denoiser 1 -Width 1920 -Height 1080 -Scenario rt_software_profile.cfg
```

The helper snapshots the base-game config and shared rendering profile into an
isolated test home and verifies that the originals remain unchanged. Samples
stay at two; saved bounces, tracing scale, filtering, exposure and bloom remain
in effect. DLSS/Frame Generation/Neural Rendering are disabled and the frame
limiter is removed for measurement. Use matching settings and the same camera
for both builds. This script selects NRD explicitly; use `-Denoiser 0` for native.

The scenario runs through `activeAction`, after the first active game snapshot,
then lets the teleport command's velocity settle. It verifies the camera before
and after measurement and rejects incomplete runs or fewer than 24 measured
frames. Earlier startup-`exec` runs could issue the camera command before the
player spawned; those are not valid comparisons. The process guard defaults to
45 seconds; `-TimeLimitSeconds 60` explicitly permits up to 60 seconds including
startup and shutdown. Check validation separately from benchmark runs. Raw
captures and local baseline DLLs remain in ignored build directories.

### Internal software-shader diagnostics

`r_softwareRayTracingProfile` is a temporary, default-off, mode-1-only control:

| Value | Diagnostic |
| --- | --- |
| 0 | Off: no diagnostic dispatch, allocation or pipeline creation on a fresh renderer |
| 1 | Shader clocks plus work counters; request before `vid_restart` to enable the optional clock feature |
| 2 | Work counters only; no shader-clock extension required |

If clocks were not enabled or are unsupported, value 1 reports a counters-only
fallback. Turning profiling off stops replay/readback logging; already allocated
diagnostic resources remain cached until renderer restart. Normal rendering does
not require the optional [Vulkan shader-clock extension](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_shader_clock.html).

The production kernel still renders the complete frame. A separate diagnostic
kernel then replays one 8x8 workgroup per 64x64 tile, rotating its selection over
64 frames. It uses the same scene and path settings, writes only its private
result buffer, and does not write production images, denoiser guides or history.
No atomic counters or additional GPU fence waits are used. At 960x540 tracing,
the result buffer is about 1.7 MiB. Both native and NRD diagnostic variants are
provided; normal rendering selects the non-diagnostic payloads.

`SW_SHADER_PROFILE` records contain exclusive per-invocation clock categories
and counts for queries, visited BVH nodes, triangle tests, mask rejections and
bounce iterations. Traversal is split into ordinary/decal, visibility and weapon
queries. Material/texture evaluation, lighting proposals, callbacks and path
continuation have separate clock categories. Node and triangle counts are work
counts, **not separate node-versus-triangle timings**.

These are **diagnostic clock shares, not GPU milliseconds, hardware utilization,
or percentages of production frame time**. Instrumentation changes register
pressure and scheduling; subgroup clock deltas can include time stalled behind
other work. Sparse sampling and scene content also affect the result. Do not
multiply these percentages by the normal trace duration to infer recoverable
milliseconds. Counters-only replay independently checks work counts but does
not remove this limitation from the timed variant.

GPU frame records containing replay are labeled `SW_PROFILE_DIAGNOSTIC` instead
of `SW_PROFILE`; their `exposure` interval includes diagnostic replay as well as
exposure work. The normal benchmark summarizer rejects contaminated intervals.

To capture the fixed-camera, two-sample, saved-settings diagnostic:

```powershell
./tests/run-software-lighting.ps1 -UseSavedSettings -Denoiser 1 -Width 1920 -Height 1080 -Scenario rt_software_diagnostic.cfg -ShaderDiagnostic 1 -TimeLimitSeconds 60
# Repeat with -ShaderDiagnostic 2 -Validation for counters-only validation.
./tests/summarize-software-diagnostic.ps1 -Log <test-home>/baseq3/qconsole.log -ExpectedMode 1
```

The scenario measures 32 frames, so it covers only half of the 64 selection
phases, not a full-screen work census. Its analyzer verifies the fixed camera,
two samples, at most six bounce iterations per path, valid records, and that
the requested diagnostic actually activated; fallback is not a timed-test pass.

### Recorded results

The [development record](archive/SOFTWARE_RAY_TRACING_DEVELOPMENT.md) preserves
the internal breakdown, matched traversal comparisons, device checks and their
limitations. Those results are not guarantees for other maps, GPUs or builds.

## Verification

```powershell
./tests/check-rt-software.ps1 -VulkanSDK C:/VulkanSDK/1.4.350.0 -DeviceIndices 0,1
./tests/run-software-lighting.ps1 -Validation -Width 640 -Height 360
```

The offscreen GPU check recompiles the shipped software kernels, checks their embedded
payloads, rejects hardware-only SPIR-V capabilities and raster-lighting inputs,
and verifies that diagnostic variants store only to their private result buffer
(no image writes or atomics). Only timed diagnostic variants may read clocks. It
then compares 32,768 GPU queries per device against independent double-precision
brute force. It checks BVH bounds/coverage, triangle indirection, alpha-style
candidate rejection, masks, barycentrics, minimum/maximum distances and
empty/static/dynamic/combined trees. Mixed-mask and world-only static fixtures
verify query-root selection, weapon misses, and a weapon behind a world occluder.
Query-test devices enable zero extensions
and optional features; the full textured integrator additionally needs the
descriptor-indexing feature described above.

The game helper creates isolated settings without DLSS/NGX/Streamline DLLs;
`-Denoiser 1` copies the optional NRD adapter into that isolated test directory.
It checks normal exit, completion markers and validation output, and closes its
own game process after 45 seconds by default (explicitly adjustable up to 90
with `-TimeLimitSeconds`). The default scenario is a functional
smoke test; use the profiling scenario above for timing comparisons. It waits
for client spawning before positioning the camera.

Recorded device and mode-switch checks are in the
[development record](archive/SOFTWARE_RAY_TRACING_DEVELOPMENT.md#checked-on-2026-09-12).
They do not replace testing the current build.
