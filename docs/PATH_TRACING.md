# Native path tracing

`r_rayTracing 2` selects VQ3 Evolution's experimental Vulkan path tracer.
It replaces world lighting with traced direct/indirect illumination and
reflection/transmission transport. It is separate from the hybrid directional
shadow overlay (`r_rayTracing 1`). The raster renderer remains available with
`r_rayTracing 0`.

This is native engine/shader code, not a replacement PK3. It remains under
development; see [current status](STATUS.md) for unresolved runtime and image
quality issues. The detailed dated work log is preserved in the
[development archive](archive/PATH_TRACING_DEVELOPMENT.md).

## Enable

Open **Shift+F10 → Lighting**, or use the console:

```text
/cl_renderer vulkan
/r_rayTracing 2
/r_dlss 5
/r_dlssRayReconstruction 1
/vid_restart
/pt_info
```

This example selects DLAA and Ray Reconstruction at native resolution. NVIDIA
features require the [optional runtimes](DLSS.md) and compatible hardware.
Path tracing itself uses Vulkan KHR ray queries, not an NVIDIA-specific tracing
API or NGX application ID. The renderer checks ray-query/acceleration-structure,
buffer-address, descriptor and storage capabilities; unsupported features must
fall back rather than being forced on. Use `/vkinfo` to identify the active GPU.

RR is the default requested reconstruction path when its prerequisites are
active. With RR off or unavailable, the native surface-aware reconstruction
path remains. [RR](RAY_RECONSTRUCTION.md) and experimental Neural Rendering are
different features; NR is bypassed while RR runs.

## Lighting and quality controls

Defaults below are source defaults, not a command to overwrite saved settings.
The [universal panel](UNIVERSAL_OPTIONS.md) exposes the main controls in loaded
games, Team Arena and mods; console changes also persist through the shared
presentation profile. RTX/DLSS/RR mode changes need `vid_restart`. The following
quality and lighting controls update live and reset relevant history.

| Console variable | Default | Range and meaning |
| --- | ---: | --- |
| `r_pathTracingSamples` | 2 | 1–64 samples per pixel per rendered frame; fixed unless adaptive sampling is explicitly enabled |
| `r_pathTracingAdaptive` | 0 | 0: fixed count; 1: eligible RR pixels may use half the requested count, including 2 → 1 |
| `r_pathTracingBounces` | 4 | 1–12 maximum surface interactions; 1 is direct-only |
| `r_pathTracingLightReuse` | 1 | Enable RR primary-surface map-light sample reuse |
| `r_pathTracingExposure` | 1 | 0.01–16 linear exposure before tone mapping; 2 doubles brightness; does not brighten HUD/menu |
| `r_pathTracingAmbient` | 0 | 0–2 diffuse ambient fill, also contributing to fog; 0 disables it |
| `r_pathTracingSunAngle` | 0 | 0–20 degrees, full angular diameter of the map's authored sun |
| `r_pathTracingLightRadius` | 0 | 0–64 world units, source radius for map point/spot and dynamic lights |

For a flat two-sample budget, use `r_pathTracingSamples 2` and
`r_pathTracingAdaptive 0`. RR does not silently change samples or bounces.

Ambient fill is an artistic fill term, not extra traced bounce lighting or an
exposure substitute. Sun angle affects only maps with an authored sun. Local
radius and sun angle sample finite light sources; they do not blur the shadow
image, resize emissive textures or affect hybrid shadow mode. Zero restores the
original point/directional-source behavior. Large radii can extend through nearby
walls and soft shadows may require more convergence.

Native reconstruction controls `r_pathTracingDenoise` (default 1),
`r_pathTracingTemporal` (1) and `r_pathTracingHistory` (8, range 1–32) apply to the
native path, not NVIDIA's RR model. The engine does not expose RR's internal
history length as a native denoiser setting.

## Scene, materials and effects

The ray scene retains world geometry independently of the raster camera and
tracks supported dynamic geometry. Material conversion preserves authored UVs,
animation, coverage and lighting declarations rather than using baked RGB
lighting as path-traced albedo. Implemented cases include:

- Diffuse/metallic/rough reflective materials, normal/material textures and
  environment-reflecting health shells/crosses that retain their authored tint.
- Cutouts and thin transparency; layered glow/emission, animated monitor panels
  and deformed hologram/power-up effects with distinct coverage semantics.
- Authored skyboxes, stock BSP mirrors, glass/water and bounded nested optics.
- Convex BSP fog volumes, segment transmittance and sampled in-scattering.
- Team Arena terrain blending using interpolated authored vertex alpha.

These are supported cases with targeted checks, not a promise that every mod
shader, portal or mixed optical path is correct. See [RR limitations](RAY_RECONSTRUCTION.md#limitations)
and the dated material regressions in the archive. The optional
[texture-upscaling tool](TEXTURE_UPSCALING.md) creates an asset override pack;
that separate offline workflow does not package renderer or UI implementations.

## Developer diagnostics

Use an isolated `fs_homepath` and `devmap` for cheat-protected diagnostics.
Do not use reference/debug/profiling modes as normal gameplay or performance
baselines. `pt_info` reports selected pipelines, actual RR state, sampling,
history and scene statistics; `nvidia_info` reports NVIDIA feature state.

| Variable | Default | Diagnostic purpose |
| --- | ---: | --- |
| `r_pathTracingReference` | 0 | Freeze submitted geometry/lights/material time for fixed-camera convergence; not game simulation |
| `r_pathTracingDebug` | 0 | 1 diffuse, 2 reflection/transmission, 3 emission, 4 normals, 5 roughness/metalness, 6 dielectric classification |
| `r_pathTracingTemporalDebug` | 0 | Native history/lighting-change views; bypasses normal RR routing |
| `r_pathTracingAdaptiveDebug` | 0 | RR budget overlay: half versus full sampling |
| `r_pathTracingProfile` | 0 | Coarse GPU timestamp logging |
| `r_pathTracingShaderProfile` | 0 | Intrusive shader-clock diagnostics; not FPS acceptance |
| `r_pathTracingRRRows` | 0 | Non-archived, restart-applied scheduling override; 0 automatic, or 8/32/64/128 rows at eight columns |
| `r_pathTracingStaged` | 0 | Rejected experimental split pipeline; leave off for normal use |

Reference accumulation requires a fixed camera and DLSS off. Switching back to
live rendering invalidates its history. Diagnostic paths can allocate/use
different buffers, so a native reference is not the normal RR memory or timing
baseline. Native fallback resources remain allocated with RR; high native
resolutions and HD textures can therefore consume substantial VRAM.

## Build and validation

Rebuild the executable and renderer together when their interface changes.
Checked-in GLSL and embedded payloads must stay synchronized. The normal Makefile
links embedded C arrays; it does not compile changed GLSL automatically. Use the
[shader and offline check instructions](TESTING.md#embedded-shaders), then build
the release renderer.

GPU comparisons must retain resolution, DLAA/DLSS mode, samples, bounces,
filtering, exposure and scene/camera. Disable Frame Generation for rendered-FPS
measurements, preserve saved settings through an isolated profile, and require
clean completion. A successful compile or one clean scene does not establish
material completeness or whole-renderer validation.

The [GPU profiling guide](GPU_PROFILING.md) covers hardware attribution;
[current status](STATUS.md) separates known failures from dated successful tests.
