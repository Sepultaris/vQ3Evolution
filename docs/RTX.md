# NVIDIA RTX ray tracing

The graphics menu now distinguishes **Shadows** (`r_rayTracing 1`) from the
experimental **Path tracing (WIP)** mode (`r_rayTracing 2`). This document covers
the shadow mode. See [PATH_TRACING.md](PATH_TRACING.md) for the larger overhaul,
implemented path-tracing core, controls, and explicit remaining work.

VQ3 Evolution contains a native Vulkan ray-query shadow path. It is a hybrid
renderer: Quake III's regular Vulkan rasterizer produces the scene, then the RTX
pass traces visibility rays through the same opaque world and model geometry to
add directional sun shadows before DLSS and the HUD are composited.

This is hardware ray tracing, not a screen-space shadow filter. When a map loads,
the renderer extracts the complete opaque BSP world independently of raster
visibility. Each frame it adds eligible dynamic model geometry, builds the
bottom-level acceleration structure (BLAS), places it in a top-level acceleration
structure (TLAS), and dispatches a compute shader using `GL_EXT_ray_query`.

Keeping the full world in the ray scene is important: using only the surfaces
visible to the raster camera would make occluders enter and leave the acceleration
structure as the player turns, producing shadows that appear attached to the
camera. First-person, depth-hacked, and explicitly no-shadow models are excluded
from world occlusion.

The world post-processing pass ends before `RDF_NOWORLDMODEL` views such as
the HUD's 3D ammo and head icons, even when no 2D picture has been drawn yet.
Those views must not overwrite the world camera, jitter its projection, modify
its depth, or enter its ray geometry. Shadow-ray reach is based on the map bounds,
not the camera-dependent raster far plane. The D32 scene pass also uses its own
compatible graphics pipelines; the presentation/HUD depth-stencil format is
unchanged. BLAS-to-TLAS synchronization covers both the BLAS read and reuse of
the shared scratch buffer for writing.

## Requirements

- The Vulkan renderer (`cl_renderer vulkan`)
- A Vulkan 1.2-capable GPU and driver exposing `VK_KHR_ray_query`,
  `VK_KHR_acceleration_structure`, and `VK_KHR_deferred_host_operations`
- Buffer device address, acceleration-structure, and ray-query device features

The renderer checks every extension and feature before requesting it. On an
unsupported GPU the option is disabled and normal raster rendering remains
available. This path does not use NGX and therefore does not need an NVIDIA
application ID or a DLSS DLL.

## Controls

The Graphics Options menu exposes **NVIDIA RTX Mode** and **RTX Shadow
Strength** (for Shadows mode). Changing the RTX mode requires applying the menu change
and restarting the renderer; strength changes are live.

| Console setting | Default | Purpose |
| --- | ---: | --- |
| `r_rayTracing` | `0` | 0: off, 1: ray shadows, 2: experimental path tracing; restart required |
| `r_rayTracingShadowStrength` | `0.55` | Blends traced occlusion from 0.0 to 1.0 |
| `r_rayTracingShadowBias` | `1.5` | World-space ray offset used to avoid self-shadowing |
| `r_rayTracingAvailable` | Read-only status | Reports whether the selected device exposes the required capabilities |

At startup, successful setup prints `NVIDIA RTX ray-query shadows initialized`.
The first real dispatch after a map loads prints the number of triangles included
in the acceleration structure. `/vkinfo` identifies the selected Vulkan device.

## Shadow-mode scope

This hybrid mode provides hard ray-traced directional shadows from
opaque map and model geometry. Alpha-tested surfaces, translucent materials,
reflection rays, emissive lighting, denoising, and full path-traced global
illumination are not represented by this shadow-only pass. Several of these
features are implemented separately in [path-tracing mode](PATH_TRACING.md).
The path-tracing ambient/penumbra controls do not change this hybrid pass.

The implementation lives in `code/renderer_vulkan/vk_raytracing.c`; the compute
shader source is `code/renderer_vulkan/shaders/rt_shadows.comp`. The generated
shader bytecode is compiled into the renderer DLL, so no replacement PK3 or
external shader file is required at runtime.

## Camera-stability regression check

Use an isolated `fs_homepath` so the test does not change a player's settings.
Copy `tests/rt_camera.cfg` into that profile's `baseq3` directory, start with
`cl_renderer vulkan`, `r_rayTracing 1`, and `devmap q3dm6`, then execute
`exec rt_camera.cfg`. The script rotates, returns, moves, restarts the renderer,
captures screenshots, and exits. Repeat with `r_dlss 0` and `r_dlss 1`.

The essential case is FFA with `cg_draw2D 1` and `cg_draw3dIcons 1`: its icons
can precede the first stretch-pic command. A HUD-hidden run alone will miss the
original bug. Compare shadow boundaries on the same world surfaces through the
turn, not the same screen coordinates. Also check that the icons and weapon
remain intact, and that restarting/re-entering the menu works.

For a diagnostic shader build, define `RT_DEBUG_REPROJECTION` when compiling
`rt_shadows.comp`. It compares the reconstructed surface distance with a primary
ray hit: green is agreement, red is a distance mismatch (saturating at four world
units), and blue is no ray hit. Material exclusions and edges can disagree;
the whole world must not turn red/blue when the HUD is enabled. Recompile without
the define and regenerate the embedded bytecode before shipping.

On 2026-09-06 the RTX 5070 test reproduced that HUD-dependent failure, verified
the corrected depth/ray match while turning (also with DLSS), and checked normal
rendering, movement, renderer restart, and the main menu at 1920x1080. Vulkan
validation no longer reported scene pipeline/render-pass incompatibility,
shared AS-scratch write hazards, or the DLSS input-depth layout mismatch.
The subsequent reconstruction update fixes raster clip-distance interfaces,
private-data enablement and several DLSS synchronization/input issues. NVIDIA
presentation diagnostics have separate dated outcomes; see [current status](STATUS.md).
This is not a claim that the entire renderer is validation-clean.
