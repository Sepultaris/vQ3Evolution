# VQ3 Evolution Vulkan renderer

The renderer is built into `renderer_vulkan_x86_64.dll` on Windows x64 and loaded
by `vQ3Evolution.exe`. Build instructions are in the [project README](../../README.md);
feature status and known failures are in [current status](../../docs/STATUS.md).

## Source map

| Area | Main files |
| --- | --- |
| Window, instance, device and presentation | `vk_create_window_SDL.c`, `vk_instance.c`, `vk_init.c`, `vk_swapchain.c`, `vk_frame.c` |
| Raster geometry and material stages | `tr_surface.c`, `tr_shade.c`, `vk_shade_geometry.c` |
| Textures, samplers and image memory | `vk_image.c`, `vk_image_sampler2.c` |
| Ray scene and hardware acceleration structures | `vk_raytracing.c` |
| Software BVH traversal and full lighting | `rt_software_bvh.h`, `shaders/pt_software*.comp`, `shaders/pt_software_query.glsl` |
| Optional software-mode denoising | `pt_software_denoise.h`, `nrd/`; separate local SDK build, see [software denoising](../../docs/SOFTWARE_DENOISING.md) |
| Path tracing and native reconstruction | `vk_pathtrace.c`, `pt_*.h`, `shaders/pt_integrator.glsl`, `shaders/pt_*.glsl` |
| Ray Reconstruction | `pt_ray_reconstruction.h`, `pt_rr_workgroup.h`, `shaders/pt_rr_*.comp` |
| NVIDIA integration and scene/output targets | `vk_streamline.cpp`, `vk_dlssnr.cpp`, `vk_ngx_bridge.cpp`, `vk_temporal.c` |
| Sharpening and night vision | `vk_sharpen.c`, `vk_nv.c`, `nv_overlay.h`, `shaders/post_nv_look.glsl` |
| Bloom package (shared post-effect passes) | `../../postfx/bloom.effect`, `../../postfx/bloom*.frag`, `../../postfx/bloom_blur.glsl` |
| Local post-effect chain | `vk_postfx.c`, `../renderercommon/postfx*.h`; engine catalog/file access in `../client/cl_postfx.c` |
| BSP camera portals and weapon-effect tagging | `pt_portal.h`, `pt_portal_transform.h`, `pt_weapon_flags.h`, `shaders/pt_muzzle_flash.glsl` |
| Console controls | `tr_cvar.c`, `tr_cvar.h` |

Raster, software and hardware path-traced modes share the renderer's scene submission
but have different lighting/reconstruction paths. Ray geometry is not limited
to surfaces visible to the raster camera. UI-only and standalone HUD/model views
must not overwrite the world camera or temporal inputs; the UI target uses
output resolution even when DLSS renders the scene at a lower resolution.

GPU work must be drained before resources are destroyed. Tagged SDK resources
must be released before their images, while Vulkan and plugin owners remain
alive through teardown. Current shutdown diagnostics are not a license to skip
cleanup or force successful exit; see the recorded NVIDIA limitations.

## Shader/build contract

GLSL sources are under `shaders/`. The matching `shaders/Compiled/*_comp.c`
arrays and SPIR-V payloads are intentional checked-in build/test inputs.
Changing GLSL requires regeneration with `shaders/compile-raytracing.ps1`,
validation and a renderer rebuild; the Makefile does not automatically compile
GLSL. See [embedded shader checks](../../docs/TESTING.md#embedded-shaders).

Optional [local post-effect packages](../../docs/POST_PROCESSING.md) use a
separate external SPIR-V/manifest loader. They do not replace the embedded
scene/reconstruction shaders. `tools/compile-postfx.ps1` compiles and validates
their GLSL without rebuilding the renderer. The current engine/renderer API is
version 15 for texture, reduced-pass, depth/motion metadata plus paired local-file callbacks; deploy matching
executable/renderer builds. Legacy mod VM interfaces are unchanged.

Do not commit `bintoc.exe`, renderer DLLs, SDK downloads, captures or local test
homes. Keep historical diagnostic variants while they remain referenced by
the Makefile or regression fixtures. Rebuild the client and renderer together
when their shared export interface changes.

## Provenance

This backend descends from vkQuake3 and work attributed there to
[Quake-III-Arena-Kenny-Edition](https://github.com/kennyalive/Quake-III-Arena-Kenny-Edition).
The [inherited notes](../../docs/archive/VULKAN_LEGACY.md) are preserved for
history; their old single-pass/interface descriptions are not current design
documentation. Preserve source copyright notices and vendored API-header licenses.
