# Current status

Status reviewed on 2026-09-13. VQ3 Evolution is a development source port, not a
finished or universally validated path-tracing release. This page summarizes
the current source and latest recorded evidence; dated test details remain in
the [documentation archive](README.md#historical-evidence).

## Implemented

- Windows x64 release build with source-built engine, renderer, base-game and
  Team Arena modules; executable name `vQ3Evolution.exe`.
- Widescreen presentation and independent HUD/menu scaling; engine-owned options
  and shared settings work independently of a mod's own menu.
- Independent live [muzzle-flash brightness and illumination controls](PATH_TRACING.md#weapon-flash-controls)
  in source-built Quake 3/Team Arena modules. Precompiled mods need explicit
  integration; these are console controls, not additional menu sliders.
- Independent live [rocket exhaust brightness and illumination controls](PATH_TRACING.md#rocket-controls)
  for source-built Quake 3/Team Arena projectiles; muzzle flashes, smoke trails
  and explosions retain their own behavior.
- Separate [rocket-explosion and lightning-gun light controls](PATH_TRACING.md#explosion-and-lightning-controls),
  covering dynamic and traced emissive illumination without dimming visible glow.
  Tested in both tracers; tagged polygons require the matching version-11 engine
  and renderer DLLs. Dark explosion/impact texture patches also reproduce in the
  pre-control build and remain unresolved.
- Fixed the path-tracer debug logger's per-frame file-handle leak, which caused
  `errno 24` failures saving console history, configs and console dumps. A
  [751-frame before/after regression](UNIVERSAL_OPTIONS.md#config-write-failures)
  reproduced the old failure and passed with the corrected logger.
- Vulkan raster, software path tracing (mode 1) and hardware path tracing (mode 2).
  OpenGL backends remain separate and do not provide Vulkan/NVIDIA features.
- Path-traced direct/indirect lighting, reflections, glass/water transport,
  animated material layers, cutouts, sky and convex BSP fog. Stock mirrors,
  pickup reflections, monitor/hologram layers and Team Arena terrain blending
  have targeted regression coverage, not exhaustive material compatibility.
- [Native BSP camera portals](PATH_TRACING.md#camera-portals) in both tracers,
  including Q3DM0's animated aperture, exposure-corrected unlit coating and distance fog. Stock
  mirrors retain their separate reflection path. Dedicated portal-object motion
  reconstruction and moving/nonplanar portal geometry remain unverified.
- DLSS/DLAA and Ray Reconstruction. RR defaults on when its prerequisites are
  selected and supported; it does not enable path tracing or DLSS by itself.
  Sampling defaults to fixed two samples, adaptive off, and four bounces.
  Existing saved values take precedence over these source defaults.
- Native reconstruction remains available when RR is off or unavailable.
  Mode 1 has local object history limits and an optional, default-off
  [NRD evaluation adapter](SOFTWARE_DENOISING.md); no distribution/license
  compatibility or game-performance improvement is claimed for that SDK path.
  WIP Neural Rendering is console-only and bypassed by RR; its runtime initializes
  only when used. WIP software tracing is also console-only; both menus offer
  Raster and Path tracing without resetting hidden preferences. Frame Generation
  retains Off/On and adds a 2x-6x multiplier slider capped to the SDK-reported
  device/runtime limit. Frame Generation and Reflex have separate capability checks.
- [Native night vision](NIGHT_VISION.md) with white/green phosphor, panoramic
  masking and grain, activated by supported Urban Terror overlays. Legacy mod
  overlays remain available when the replacement cannot run or is disabled.
- Large texture uploads grow their staging allocation, and the image-memory
  chunk table grows safely. This fixes the observed 2048x2048 upload overflow;
  it does not remove the renderer's current 2048 texture-size cap.

## Open issues and verification boundaries

| Area | Current boundary |
| --- | --- |
| NVIDIA shutdown | A 2026-09-13 current-renderer stack confirms NGX waiting in `NvTelemetryAPI64!UninitializeTelemetry`. An approved `NvContainerLocalSystem` restart succeeded but raster/DLAA and RTX/RR still required their guards after completing the NV scenario. Underlying cause remains unresolved; see [NV repair evidence](archive/NIGHT_VISION_DEVELOPMENT.md#rasterdlaa-repair-and-remaining-lifecycle-fault-2026-09-13). |
| Raster/DLAA post-processing | Black world repaired: sampled usage for sharpen output, initial bloom/NV layouts, combined depth/stencil transitions and raster motion-input layout corrected. Urban Terror on/off captures inspected; no raster Vulkan errors, but a stock NVIDIA motion-image format warning remains. RTX/RR also renders the NV sequence but still reports descriptor-lifetime validation errors. |
| Frame Generation / restart | The 2026-09-13 attended 2x retest passed native Windows foreground checks and sustained exactly 2x in stationary/moving gameplay, exiting normally in 14.6 seconds with settings unchanged. The earlier 2x-4x failures used inaccurate SDL focus diagnostics; the SDK was correctly rejecting background interpolation. Earlier tests observed 5x/6x, but native-foreground verification of 3x-6x remains pending. NVIDIA presentation validation errors and older restart/shutdown issues remain open. See [multiplier evidence](DLSS.md#multiplier-verification-2026-09-13). Do not count generated frames as rendered-FPS improvement. |
| Vulkan validation | Several scoped FG-off runs passed. The muzzle-control mode-1 run was clean; mode 2 reported descriptor-update/invalid-command-buffer errors also reproduced with the unchanged pre-control backup. The True Combat menu audit separately reproduced depth/stencil errors with the original mod. There is no whole-renderer validation-clean claim. |
| RR image quality | Rough reflections use an approximate hit-distance guide; dedicated specular object motion and separate transparency guides remain incomplete. Fine texture motion, particles and complex glass/water paths need broader validation. |
| Materials and effects | Native nested optics are bounded; arbitrary mod shader programs and moving/nonplanar portals are not guaranteed. Planar BSP camera portals have targeted Q3DM0 coverage. Unsupported cases remain visible development work, not presumed supported. |
| Platforms / mods | Windows x64 is the primary tested build. Inherited Linux/macOS and arbitrary third-party mods need their own builds/tests. The optional True Combat patch is version-specific and changes its archive checksum. |

## Performance evidence

Lower internal tracing resolution is available through `r_pathTracingScale`;
DLAA does not require tracing at display resolution. See the
[render-scale controls](PATH_TRACING.md) and [RR guide](RAY_RECONSTRUCTION.md).
There is no general FPS guarantee for a map, GPU or quality configuration.

The latest recorded mode-1 direction-order comparison reduced GPU frame time
from 111.5 ms to 79.5–79.7 ms (about 29%) in one Q3DM6 A/B/A/B test with the same
resolution, two samples, six bounces and NRD. The earlier static-tree skip saved
about 1.2–1.3 ms. Full settings, prior traversal comparisons, internal clock
shares and device-validation limits are preserved in the
[software development record](archive/SOFTWARE_RAY_TRACING_DEVELOPMENT.md).
These are software-tracer results, not mode-2 optimizations or older-GPU promises.

The combined-radiance RR path and automatic 8x128 selection remain. The later
8x64/8x32 trial did not improve performance; neither did the default-off
[dynamic opaque experiment](archive/DYNAMIC_OPAQUE.md). Historical RR comparisons
used different bounce counts and some required shutdown guards. Keep those
caveats with the [dated measurements](archive/RAY_RECONSTRUCTION_DEVELOPMENT.md).
The [2026-09-09 Nsight capture](archive/GPU_PROFILE_20260909.md) profiles that
build, not subsequent software/material/NV work.

For a source commit, run the [offline/repository checks](TESTING.md) and record
remaining runtime failures explicitly. A successful build is not a release
certification, a benchmark, or proof that these open issues are fixed.
