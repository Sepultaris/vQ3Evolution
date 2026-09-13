# VQ3 Evolution documentation

Start with the [project README](../README.md) for building and game-data setup,
then [current status](STATUS.md) for supported paths and known limitations.

## User and developer guides

| Document | Purpose |
| --- | --- |
| [Universal options](UNIVERSAL_OPTIONS.md) | Engine-owned settings for base Quake III, Team Arena and mods; shared profiles and legacy UI scaling |
| [Path tracing](PATH_TRACING.md) | Lighting, samples, bounces, exposure, ambient fill, penumbra, materials and fallback |
| [Ray Reconstruction](RAY_RECONSTRUCTION.md) | NVIDIA RR with DLAA/DLSS, current defaults, input contract and limitations |
| [NVIDIA integration](DLSS.md) | SDK/runtime setup, DLSS/DLAA, console-only WIP NR, Frame Generation multipliers and Reflex |
| [Night-vision goggles](NIGHT_VISION.md) | White/green phosphor appearance, soft panoramic optics, grain and console controls |
| [Software path tracing](RTX.md) | Console-only WIP compute-based lighting without hardware RT; mode 2 remains the hardware backend |
| [Software denoising](SOFTWARE_DENOISING.md) | Local native history and optional NRD evaluation, controls, build and licensing boundary |
| [Texture upscaling](TEXTURE_UPSCALING.md) | Optional offline asset processing, packaging and safeguards |
| [True Combat patch](TRUECOMBAT_PATCH.md) | Optional version-checked fix for the mod's own widescreen UI bug |
| [Testing and commit checks](TESTING.md) | Offline checks, shader payloads, bounded GPU tests and foreground Frame Generation verification |
| [GPU profiling](GPU_PROFILING.md) | Nsight capture and evidence acceptance |
| [Vulkan backend overview](../code/renderer_vulkan/README.md) | Source layout and rendering ownership |

## Historical evidence

- [Path-tracing development record](archive/PATH_TRACING_DEVELOPMENT.md): dated
  implementation details, material fixes, successful checks and rejected trials.
- [RR development record](archive/RAY_RECONSTRUCTION_DEVELOPMENT.md): dated RR
  integration, sampling, jitter, performance and shutdown evidence.
- [Software-tracer development record](archive/SOFTWARE_RAY_TRACING_DEVELOPMENT.md):
  traversal comparisons, internal work counters, device and mode-switch checks.
- [Night-vision development record](archive/NIGHT_VISION_DEVELOPMENT.md): actual
  mod toggles, raster/DLAA image repair and unresolved NVIDIA shutdown/validation.
- [GPU performance analysis](archive/GPU_PERFORMANCE_ANALYSIS.md): the measured
  **pre-RR** register/latency bottleneck and its follow-up; not a profile of the
  current RR shader.
- [RR capture from 2026-09-09](archive/GPU_PROFILE_20260909.md): profiler attachment
  repair, source-line findings and follow-through for that build.
- [Dynamic opaque experiment](archive/DYNAMIC_OPAQUE.md): no measured median FPS
  gain; remains default-off. The old deployment statements are historical.
- [Inherited Vulkan notes](archive/VULKAN_LEGACY.md): upstream provenance only.

Archived statements about defaults, completion or next steps apply to their
dated build. Raw captures live in ignored local build directories and are not
part of a fresh clone. Do not turn a timed-out diagnostic or one-map result into
a general performance or validation-clean claim.

Licenses and attribution remain in [COPYING.txt](../COPYING.txt),
[id-readme.txt](../id-readme.txt), component source headers, and the
[NR integration notice](DLSSNR-THIRD-PARTY-NOTICE.txt).
