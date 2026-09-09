# Current status

Status reviewed on 2026-09-09. VQ3 Evolution is a development source port, not a
finished or universally validated path-tracing release. This page summarizes
the current source and latest recorded evidence; dated test details remain in
the [documentation archive](README.md#historical-evidence).

## Implemented

- Windows x64 release build with source-built engine, renderer, base-game and
  Team Arena modules; executable name `vQ3Evolution.exe`.
- Widescreen presentation and independent HUD/menu scaling; engine-owned options
  and shared settings work independently of a mod's own menu.
- Vulkan raster, hybrid ray-query shadows and experimental native path tracing.
  OpenGL backends remain separate and do not provide Vulkan/NVIDIA features.
- Path-traced direct/indirect lighting, reflections, glass/water transport,
  animated material layers, cutouts, sky and convex BSP fog. Stock mirrors,
  pickup reflections, monitor/hologram layers and Team Arena terrain blending
  have targeted regression coverage, not exhaustive material compatibility.
- DLSS/DLAA and Ray Reconstruction. RR defaults on when its prerequisites are
  selected and supported; it does not enable path tracing or DLSS by itself.
  Sampling defaults to fixed two samples, adaptive off, and four bounces.
  Existing saved values take precedence over these source defaults.
- Native reconstruction remains available when RR is off or unavailable.
  Experimental Neural Rendering is bypassed by RR; its runtime initializes
  only when used. Frame Generation and Reflex have separate capability checks.
- Large texture uploads grow their staging allocation, and the image-memory
  chunk table grows safely. This fixes the observed 2048x2048 upload overflow;
  it does not remove the renderer's current 2048 texture-size cap.

## Open issues and verification boundaries

| Area | Current boundary |
| --- | --- |
| NVIDIA shutdown | Recent RR comparisons reach the 45-second guard after finishing their rendering script. An NR/SR diagnostic captured NGX shutdown waiting in NVIDIA telemetry cleanup. The worker's underlying wait cause remains unresolved; this is not proof that every timeout has the same cause. |
| Frame Generation / restart | Activation has varied between lifetimes; historical FG-on validation and restart failures are not closed by later FG-off successes. Check `nvidia_info`, actual presented-frame counters and window focus. Do not count generated frames as rendered-FPS improvement. |
| Vulkan validation | Several scoped FG-off runs passed. The True Combat menu audit separately reproduced depth/stencil validation errors with the original mod. There is no whole-renderer validation-clean claim. |
| RR image quality | Rough reflections use an approximate hit-distance guide; dedicated specular object motion and separate transparency guides remain incomplete. Fine texture motion, particles and complex glass/water paths need broader validation. |
| Materials and effects | Native nested optics are bounded; arbitrary mod shader programs and remote portals are not guaranteed. Unsupported cases remain visible development work, not presumed supported. |
| Platforms / mods | Windows x64 is the primary tested build. Inherited Linux/macOS and arbitrary third-party mods need their own builds/tests. The optional True Combat patch is version-specific and changes its archive checksum. |

## Latest performance decision

The combined-radiance RR implementation remains enabled. The subsequent
8x64/8x32 workgroup experiment was slower than 8x128, so the existing automatic
selection was retained. No speedup is claimed from that experiment or from
deferring unused NR initialization.

The latest comparison used DLAA, fixed two samples, **six saved bounces**, and
FG/NR off. The earlier combined-versus-separated comparison used two bounces.
Both sets of rendering-phase measurements are provisional because their game
processes did not exit cleanly. See the
[dated RR results](archive/RAY_RECONSTRUCTION_DEVELOPMENT.md#rr-workgroup-experiment-2026-09-09)
for timings, camera/configuration checks and exclusions.

The 2026-09-09 RTX 5070 capability query reports 1,024 compute invocations per
workgroup. At eight columns, 128 rows already reaches that limit; 8x256 is not
a supported experiment on that device. Alternate 1,024-thread shapes have been
discussed, **not implemented or measured**.

For a source commit, run the [offline/repository checks](TESTING.md) and record
remaining runtime failures explicitly. A successful build is not a release
certification, a benchmark, or proof that these open issues are fixed.
