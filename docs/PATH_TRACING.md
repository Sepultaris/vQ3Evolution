# Native path tracing

`r_rayTracing 2` selects VQ3 Evolution's experimental Vulkan path tracer.
It replaces world lighting with traced direct/indirect illumination and
reflection/transmission transport. Mode 1 uses the separate software-BVH compute
path tracer; see [software tracing](RTX.md). The raster renderer remains available with
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
| `r_muzzleFlashBrightness` | 1 | 0–8 visible muzzle-flash emission; 0 off, 1 original, 2 doubles it |
| `r_muzzleFlashLightScale` | 1 | 0–8 muzzle illumination, including its dynamic light and traced emissive light; independent of visible brightness |
| `r_rocketBrightness` | 1 | 0–8 visible flying-rocket exhaust/glow; opaque body reflectance is unchanged |
| `r_rocketLightScale` | 1 | 0–8 flying-rocket illumination: its moving dynamic light and traced exhaust emission; separate from muzzle flashes and explosions |
| `r_rocketExplosionLightScale` | 1 | 0–8 rocket-impact illumination, including the dynamic light and traced explosion/animated-particle emission; visible effects unchanged |
| `r_lightningGunLightScale` | 1 | 0–8 lightning beam, impact and muzzle illumination; also multiplies the existing muzzle-light setting at the muzzle only; visible effects unchanged |
| `r_pathTracingSunAngle` | 0 | 0–20 degrees, full angular diameter of the map's authored sun |
| `r_pathTracingSunScale` | 1 | 0–16 linear multiplier for the map's authored sun brightness; 2 doubles direct/indirect sunlight |
| `r_pathTracingLightRadius` | 0 | 0–64 world units, source radius for map point/spot and dynamic lights |
| `r_pathTracingAutoExposure` | 0 | 1: automatic exposure from the traced HDR average; manual exposure is bypassed, then reseeded when enabled |
| `r_pathTracingAdaptiveSpeed` | 0.25 | 0.01–10 smoothing time constant (seconds) for the auto-exposure adaptation |
| `r_pathTracingAdaptiveTarget` | 0.18 | 0.0001–20 ACES input level the frame average is mapped to; 0.18 is middle gray, 1.0 is near-white, 2+ overexposes/saturates |
| `r_pathTracingAdaptiveMin` | 0.05 | 0–64 minimum auto exposure (never below this when auto is enabled) |
| `r_pathTracingAdaptiveMax` | 16 | 0–4096 maximum auto exposure (never above this when auto is enabled) |
| `r_pathTracingScale` | 1 | 0.25–1 internal render scale for path tracing; applied on `vid_restart` |

For a flat two-sample budget, use `r_pathTracingSamples 2` and
`r_pathTracingAdaptive 0`. RR does not silently change samples or bounces.

### Rocket controls

Dim the flying rocket's exhaust and the light it casts independently:

```text
/r_rocketBrightness 0.25
/r_rocketLightScale 0.25
```

Both are console-only, live, archived and included in the shared rendering
profile. Range: 0–8; 1 restores original strength. Brightness affects visible
emission, not the opaque rocket body, alpha coverage, smoke trails, launcher,
muzzle flash or explosion. The light control changes both rocket dynamic-light
RGB (not radius) and traced emitter radiance/selection weights. It can also dim
the rocket body's lighting where the body is illuminated by its own light.
Camera/ideal mirror/refraction paths use brightness; diffuse/rough illumination
uses light strength. Changing either invalidates traced lighting history.

Source-built base-game and Team Arena native/bytecode cgame tag flying rockets
with `RF_ROCKET`; precompiled mods must integrate the tag and dynamic-light
control to participate. Both software and hardware tracing support the controls.
Vulkan raster dims only additive rocket stages; its 8-bit colors clip boosts.
OpenGL does not implement the separate visible-emission multiplier. Defaults
remain 1; the example above is a starting preference, not a required setting.

Verification (2026-09-13): engine, renderer and both native/bytecode game modules
built; all 29 affected shaders passed SPIR-V validation. Normal/release fixtures
passed shared-profile persistence, bounded scale handling and 300,000 independent
rocket/muzzle emitter cases with matching cached/uncached light PDFs. Q3DM6
captures from `rt_rocket_brightness.cfg` verified all five live states and 74
tagged rocket triangles in mode 2/native (`run-60882281b6ab453c942017cbc41c2c7b`,
19.6 seconds) and mode 1/NRD/bytecode (`run-8e8754abc1ba4168bc68b2317de4bcb7`,
44.7 seconds), under `build-widescreen/software-lighting-audit/`. Both saved the
controls and exited normally without changing user settings. Earlier attempts
timed out or failed to fire a rocket; they are not passes. The final captures
used 1280x720 output with saved 0.5 tracing scale; these are functional tests,
not performance measurements. RR/FG/NR were disabled; the previously documented
hardware validation issues are not resolved by this change.

### Explosion and lightning controls

Rocket impacts and the lightning gun have their own live, console-only light controls:

```text
/r_rocketExplosionLightScale 0.25
/r_lightningGunLightScale 0.25
```

Range 0–8, default 1: 0 disables that effect's illumination and 1 restores its
original strength. Values are archived in the shared rendering profile. These
controls scale light, not visible glow, alpha coverage, light radius or lifetime.
Rocket impact sprites and animated explosion polygons are explicitly tagged;
grenade/BFG explosions, flying rockets, smoke trails and map lights are excluded.
The lightning control covers the beam, impact and muzzle; at its muzzle it
multiplies `r_muzzleFlashLightScale` so the existing muzzle control still works.
The grappling hook is not tagged, even though it uses the same lightning shader.
Both tracers use the same scales for emitter selection, direct and bounced
illumination; visible camera/ideal specular emission remains unchanged. Live
changes invalidate traced lighting history, including during an existing blast.

The engine/renderer interface is now version 11 for optional tagged polygon
submission (`CG_R_ADDPOLYTAGGED`, 201). This preserves the explosion particle's
original vertices, UVs, animation and colors without shader-name heuristics or
new GPU vertex/ray storage. Install the matching engine and all renderer DLLs
together. OpenGL uses ordinary polygon submission and supports the scaled
dynamic lights, not traced emissive lighting. Third-party precompiled mods
need explicit source integration for these weapon-specific tags/controls.

Verification (2026-09-13): engine, all three renderer DLLs and base-game/Team Arena
native and bytecode cgame builds passed; 29 affected shaders passed SPIR-V
validation. Shared-profile and tag/scale checks passed at normal and release
optimization, as did 300,000 cached/uncached emitter cases (including combined
lightning/muzzle controls and unchanged visible emission).
`rt_weapon_effect_lights.cfg` passed all on/off/0.25 states in Q3DM6, with four
tagged explosion triangles (sprite and animated polygon), 62 tagged lightning
triangles, saved controls and normal exits: mode 2/native at 1280x720,
`run-7ebc26dc626546a7bfa6b1a430bde481` (18.0s); mode 1/bytecode/native denoiser
at 960x540, `run-074aa3e57e4542fcb93bc4bcd9da5361` (25.6s). Both used saved
0.5 tracing scale and isolated settings, with RR/FG/NR disabled. These are
functional checks, not performance measurements or new validation-layer passes.

The captures also expose dark rectangular explosion/impact texture artifacts.
The unmodified pre-control engine/renderer reproduced them in
`run-29e0bb05523c4343a7185a39866be956` (normal 18.0s exit; new-control assertions
expectedly fail on the old build). That material issue remains unresolved;
these light controls do not fix or newly introduce it. Audit directories are
under `build-widescreen/software-lighting-audit/`.

### Weapon flash controls

For example, dim the visible flash to half brightness and its illumination to
one quarter:

```text
/r_muzzleFlashBrightness 0.5
/r_muzzleFlashLightScale 0.25
```

Both are console-only, update live without `vid_restart`, and persist in the
shared rendering profile. Set either to 1 to restore its original strength or
0 to disable that contribution. Changing them resets traced lighting history.
The light multiplier preserves the dynamic light's radius and color ratios;
the flash model's emission and light-sampling weights use the same multiplier.
Visible emission on camera and ideal mirror/refraction paths uses the separate
brightness multiplier; illumination sampled through diffuse/rough scattering
uses the light multiplier. These controls do not scale lightning beams,
projectile/explosion effects or map lights.

Source-built Quake 3 and Team Arena game modules explicitly tag muzzle-flash
entities (`RF_MUZZLE_FLASH`) and scale only muzzle dynamic lights. Install both
the native and loose bytecode game modules so either `vm_cgame` mode works;
no PK3 replacement is required. Precompiled third-party mods need the same
source integration; the engine deliberately does not guess from shader names,
light positions or radii. Vulkan raster also supports visible-flash dimming,
but its existing 8-bit stage colors can clip boosts above 1. OpenGL does not
implement the separate visible-flash multiplier.

Verification (2026-09-12): engine/renderer and both native/bytecode cgame builds
passed. The scale fixture, shared-profile checks and 300,000 packed/unpacked
emitter cases passed at normal and release optimization; affected shaders passed
SPIR-V validation. In Q3DM6, a mode-1/NRD bytecode run captured all five live
control combinations with 48 tagged flash triangles and an empty synchronization
validation log (`software-lighting-audit/run-dd2b75b90fa345eeb133b383bb778197`).
A mode-2 native-cgame run also showed independent controls, but validation failed
with descriptor-update/invalid-command-buffer errors
(`run-1be49fc929b9472d8bf15a67305618a0`). The unchanged pre-control backup reproduced
the same 36 errors in the same 15 VUID categories
(`run-52d8d1c6af9d4ffd820626c9563677c5`); this existing hardware-renderer issue is
not fixed by the brightness controls. Its feature-specific assertions fail as
expected because the backup has no muzzle controls. Completed runs closed in
about 20–40 seconds with saved settings untouched and Frame Generation disabled.
Auto exposure remained enabled, so these are functional image checks, not linear
brightness measurements. Team Arena was build-checked, not separately play-tested;
RR and raster were not runtime-tested for these controls. No performance claim.

### Render scale

`r_pathTracingScale` (default 1, range 0.25–1) runs the entire path-tracing
pipeline at a fraction of the internal render resolution, then presents the
result at full resolution through reconstruction or the final upscale. Trace
time scales with the rendered pixel count, so 0.5 gives roughly a 4× trace
speedup on the reference scene (e.g. 19.4 → 5.1 ms trace at 1920×1080).

Scales that match an NVIDIA DLSS quality ratio (0.25, 0.333, 0.5, 0.667) keep
DLSS/Ray Reconstruction evaluation active at that exact render ratio; the DLSS
mode is remapped to the matching preset (Ray Reconstruction then upscales the
half-resolution render for quality) and DLSS optimal-settings render sizing is
bypassed so the scale is authoritative. Any other scale disables RR evaluation
for the session and the linear present upscale is used, which the engine also
uses whenever NVIDIA runtimes are absent. The setting is latched: apply it and
`vid_restart`. Compare at fixed samples, bounces, lighting mode, scene and
camera; the savings come from fewer traced pixels, not changed sampling.

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
- BSP camera portals, including Q3DM0's wave-deformed aperture and layered fog.
- Convex BSP fog volumes, segment transmittance and sampled in-scattering.
- Team Arena terrain blending using interpolated authored vertex alpha.

These are supported cases with targeted checks, not a promise that every mod
shader, portal or mixed optical path is correct. See [RR limitations](RAY_RECONSTRUCTION.md#limitations)
and the dated material regressions in the archive. The optional
[texture-upscaling tool](TEXTURE_UPSCALING.md) creates an asset override pack;
that separate offline workflow does not package renderer or UI implementations.

### Camera portals

Both tracers include entity-owned planar BSP camera portals. A ray entering the
aperture is transformed to the server's portal camera, including its roll and
rotation, and continues through the actual ray-traced scene. This is not a
screen-space reflection, cubemap, or raster fallback. The undeformed BSP plane
anchors the camera while the aperture retains its vertex animation. Ordered
alpha, multiplicative, additive and distance-fog stages coat the remote view.
The existing stock mirror path stays separate.

There are up to 63 portal surfaces per world, with a shared 32-layer continuation
limit preventing cyclic paths. No extra per-pixel buffers or ray samples were
added; the camera table adds 4032 bytes to the scene-light buffer. `r_noportals 1`
disables the remote camera contribution, leaving the authored coating.
`pt_info` reports the bound cameras and their material programs.

Q3DM0 checks cover front/angled/distant views, camera enabled/disabled, and its
adjacent mirror. CPU tests exercise the production camera transform and ordered
layer compositor. Both software and hardware native-reconstruction runs completed;
the software run also passed scoped Vulkan validation. These are visual checks,
not performance benchmarks. Test configurations use isolated settings.

The latest brightness comparison used unchanged saved exposure/bloom settings
at four camera positions. Both hardware native-reconstruction runs exited cleanly
(20.6 seconds before, 19.5 seconds after); front and angled captures show the
destination through the corrected coating instead of white washout. These run
durations are not frame-time measurements. A DLAA/RR check captured the same four
views and reported 109 evaluated RR frames and the scenario completion marker.
It then stalled at `PT_SDK_SHUTDOWN_BEGIN` at 52.4 seconds; the 60-second guard
terminated it. Thus RR visual rendering was exercised, but this is not a clean
RR process-lifecycle pass. The earlier portal implementation check also covered
the adjacent mirror; it encountered the same SDK shutdown issue.

Portal artwork is an unlit display coating, not physical scene emission. Its
ordered stages now compose in texture space, then the inverse filmic curve and
current exposure convert that coating to radiance. This prevents scene exposure
from washing the fog white; the remote scene still receives normal HDR lighting
and exposure. Transmission factors and the authored distance fade are unchanged.
The CPU fixture executes the production compositor and forward tone mapper over
all 256 byte shades, alpha fades and exposure 0.01–16. This calibrates the coating
over black; it is not an exact display-space blend against every HDR background.
Path-traced destination lighting is not the raster's baked lighting.

Moving brush portals, arbitrary nonplanar portal geometry, and dedicated motion
reconstruction for objects seen through a portal remain outside this coverage.
Guides retain the aperture's depth and mark the animated composite reactive rather
than treating the remote image as a stationary local mirror.

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
