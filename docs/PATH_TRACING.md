# Native path-tracing renderer

## Target and completion criteria

The requested target is a visual-overhaul mode comparable in scope to a
path-traced game renderer, not the existing directional shadow overlay.
The existing raster and ray-shadow modes remain available during development.
Do not label an intermediate implementation as finished full RTX.

## Current implementation: experimental native path tracer

`r_rayTracing 2` selects the native compute/ray-query path integrator, exposed as
**Path tracing (WIP)** in Graphics Options. World pixels come from textured
primary-ray hits and simulated light transport, not from the baked scene color.
Normals/UVs and per-triangle material IDs are uploaded alongside the ray geometry.
The initial material bridge retains `q3map_surfacelight` and `q3map_lightimage`;
an optional shader-level `rt_material <roughness> <metallic>` declaration controls
the GGX material. Native defaults identify legacy metal/chrome and stone/concrete
names; other opaque shaders default to a rough dielectric. These are conservative
artistic defaults, not measured materials; authored properties take precedence.

The integrator implements area/power-weighted emissive-triangle sampling, diffuse/GGX
scattering, multiple-importance sampling, Russian roulette, directional sun,
map point/spot lights and game point lights, alpha-test confirmation in primary/secondary/shadow rays,
floating-point radiance accumulation and tone mapping. It uses a simple explicit
sky environment pending map-specific HDR sky/material translation. Textures are
sampled without the raster gamma/intensity bake in this mode.

Map lights are read from the BSP entity data, including target-based spotlights,
instead of being reconstructed from lightmaps. Map-light selection uses a
distance/power-weighted reservoir of eight uniformly selected candidates at each
shading point, with candidate-count compensation in the lighting estimator.
Point emission uses the
original map compiler's `pointScale=7500`, converted into the path tracer's
1/1000 compiler-unit radiance convention. This is a legacy-unit translation,
not an SI-calibrated or artist-validated lighting conversion. Legacy styled or
linear lights currently use steady inverse-square emission and report a warning.
Sky boundaries are identified by `SURF_SKY` as well as `skyparms`; texture-only
skies such as `blacksky` retain their source appearance and do not block sun rays.
Sun parameters are retained per shader and selected from the loaded map, avoiding
dependence on shader-cache load order.

MD3 meshes use LOD 0 and are submitted outside the camera frustum for ray hits.
Moving brush entities and deformed world surfaces also bypass camera culling for
ray-scene submission. Third-person-only body geometry is excluded from primary
rays but retained for secondary rays; `RF_NOSHADOW` is a shadow-only exclusion.
This uses entities the game supplies, not server-hidden/network-unavailable ones.

First-person weapon meshes now have explicit primary-ray visibility priority and
are shaded by the path integrator. There is no raster weapon color/depth copy.
The third-person body/weapon representation serves secondary rays. Weapon effects
and layered materials remain subject to the incomplete material bridge below.
HUD and menu views are still rendered separately after the path-traced scene.

Native reconstruction reprojects separate diffuse and reflection linear radiance using primary-hit
world positions and the previous camera, including projection jitter. Bilinear
history taps must agree in material, normal, surface plane and local world
distance. Newly exposed surfaces start fresh. A local radiance envelope clips
history before an initial three-pass a-trous spatial filter guided by world
position, geometric and mapped shading normals, material, albedo and roughness. The spatial result
never feeds temporal history or the separate reference accumulator.

Temporal history defaults to eight frames, capped at four when scene geometry
changes. Reflection history has its own count (two to eight according to roughness),
view-change rejection and local radiance variance/clipping; diffuse history is not
shortened just because a surface is glossy. Abrupt view changes, FOV/render
settings changes, invalid frame gaps, map changes and game point-light changes
(including muzzle flashes) reset it. Animated emitter CDF/index changes are
handled as geometry motion, not mistaken for a global radiance change. Ordinary
camera movement does not discard all history. Built-in cgame submissions carry
stable entity/part IDs and teleport generations through a new tracked renderer
entry point. The renderer matches previous vertices by identity, model/material,
topology and surface ordinal; barycentric hit positions map animated poses back
to their previous positions/normals. This covers tracked players, weapons,
items, missiles and brush movers. Changed topology, teleport generation, missing
previous geometry or large discontinuities reject history. Ordinary legacy/mod
submissions remain supported but are untracked and do not reuse object history.
Low-roughness reflections also reject substantial view
direction changes. Sky and directly visible emitters currently bypass filtering.

DLSS now receives R32 path-hit depth and RG32 normalized motion containing both
camera and tracked-object movement, including ray-shaded first-person geometry.
Motion excludes projection jitter and uses previous-minus-current UV convention.
Untracked effects still receive camera-only motion. Additive/filter layers are
traversed to find the underlying opaque surface for depth/motion; visible effects
reject native radiance history instead of masquerading as opaque guide surfaces.
Temporal resets propagate to DLSS. These inputs do not provide reflected-object motion or layered transparency
motion; a single primary surface is still insufficient for those cases.

The material bridge preserves independent source stages before raster stage
collapsing. It evaluates animated image frames, ordered UV transforms (scroll,
scale, rotation, stretch, turbulence, affine and entity translation), vector UVs,
entity/vertex/constant colors and color/alpha waveforms. Clamp-map addressing is
retained. World vertex lighting is not multiplied into albedo. All eight possible
source additive stages can contribute emission with independent animation,
including crossfaded flames; an emitting surface without a compiler radiance
declaration defaults to one renderer radiance unit. This is an explicit legacy
material conversion, not physically calibrated power.

Pure additive layers transmit the underlying scene and do not cast opaque
shadows. Standard source-alpha transparency uses stochastic coverage in camera,
secondary and visibility rays. Multiplicative filter materials tint straight
transmission and light visibility. Traversal is bounded to 32 transparent layers.
These legacy effects remain coverage/filter models. Actual water and glass use
the separate dielectric transport described below. Animated surface materials reject native history to avoid reusing
old texture/color phases. Reference mode freezes material time as well as geometry.

This is **not the completed renderer or a production denoiser**. Separate
diffuse/reflection reconstruction is implemented; localized reactive lighting
handling remains unfinished. Global point-light resets and conservative per-surface rejection
can still leave noisy gameplay; reflected motion and moving
shadows need broader trail/flicker testing. This native pass is not NVIDIA Ray
Reconstruction, NRD or a complete SVGF implementation. The first non-additive
base layer is used; arbitrary multi-base blend composition,
fog, portals and non-MD3 animation completeness remain
unfinished. Legacy environment-map tricks are skipped in favor of the BRDF;
portal/specular alpha generators and exact legacy noise-wave equivalence are
not implemented. Submitted dynamic sprites/beams can now enter the ray scene,
but complete particle/decal capture, coplanar powerup-shell composition,
reactive DLSS masks and off-camera billboard orientation need further work.
Static and
dynamic geometry currently share a rebuilt acceleration structure; persistent
per-object structures and performance optimization are also unfinished. DLSS
compatibility does not imply finished reconstruction quality. The current PT
texture sampler is not yet connected to the full raster filtering/mip controls.

### Independent lighting reconstruction

The integrator partitions every contribution by the **first scattering lobe**.
It evaluates both diffuse and GGX terms at the same sampled direction and
propagates their weighted throughputs independently. This is not a random
diffuse/specular label on a combined sample: the two terms plus directly visible
emission/environment sum to the original light-transport estimator. Reflection
also contains specular transmission through dielectrics. Subsequent bounces may
contain either lobe, as usual for a primary-lobe decomposition.

Each lighting channel has separate raw radiance, double-buffered temporal history,
history count, neighborhood clipping, variance estimate and spatial scratch buffers.
Reflection rejection additionally checks view direction, mapped normal and
roughness. Mirror-like reflection/transmission never receives spatial blur;
rougher lobes permit progressively wider filter passes. Directly visible emission
is composited afterward, unfiltered. Reference accumulation is also separate for
all three channels and never receives denoised feedback. No diffuse albedo division
is applied to metallic reflections. This is a native implementation, not NVIDIA
Ray Reconstruction or a port of Q2RTX's multi-resolution HF/LF/SPEC denoiser.

### Texture-driven materials and dielectric transport

Native shader declarations (outside stage braces):

| Declaration | Meaning |
| --- | --- |
| `rt_material roughness metallic` | Opaque GGX defaults; roughness 0.02–1, metalness 0–1 |
| `rt_basecolormap image` | Dedicated sRGB base color, independent of legacy light stages |
| `rt_normalmap image` | Linear tangent-space XYZ normal map, +Y bitangent convention |
| `rt_normalscale value` | Tangent-space XY strength, 0–4; default 1 |
| `rt_ormmap image` | Linear G roughness / B metalness; R baked AO is intentionally not applied to physical lighting |
| `q3map_lightimage image` / `q3map_surfacelight power` | Independent emissive map and power |
| `rt_dielectric ior thickness` | IOR 1–3; zero thickness is a closed volume, positive thickness is a legacy parallel pane in Quake units |
| `rt_absorption r g b` | Nonnegative Beer–Lambert extinction per Quake unit, linear RGB |

PBR maps use the mesh's original UV set. Source animated stage UVs remain supported
for legacy albedo/emission; animated PBR-map transforms are not yet exposed.
Tangents are derived from actual triangle positions/UVs, including mirrored UV
handedness. Degenerate UVs fall back to the interpolated normal. Scalar maps bypass
sRGB conversion. Normal-map shading remains constrained to the geometric hemisphere.

Stock `CONTENTS_WATER` surfaces become IOR 1.333 water, with world-anchored moving
wave normals and colored absorption. Translucent, non-alpha-tested glass-named
surfaces become IOR 1.5 panes; cutout glass frames remain opaque/cutout. Legacy
colored glass textures become absorption color, rather than emissive overlays.
No new material asset package is required. `vqe/glass`, `vqe/water` and `vqe/metal`
are also native source-defined material names. Optional author textures can be
ordinary loose game assets; all interpretation/transport code is compiled in.

Dielectrics trace real reflection/refraction rays using unpolarized Fresnel,
Snell's law, total internal reflection, radiance-mode IOR scaling and distance-based
absorption on path and next-event visibility segments. Up to four nested media are supported. A parallel pane includes both
interfaces' energy and internal-reflection attenuation, with a laterally displaced
transmitted ray; IOR 1 has zero displacement. Closed water/glass boundaries update
the medium stack. Static BSP water brush planes initialize the camera medium, so
looking at a submerged floor receives absorption even before a water boundary is
hit. Crossing the water line invalidates lighting history.

Interfaces are optically smooth, optionally normal-mapped/wavy; roughness does
not yet implement a rough microfacet transmission BSDF. Transmission is
reconstructed in the specular channel using the interface guide. Separate
reflected/refracted-object motion, specialized caustic sampling (especially point
lights through refractors), scattering fog and physically layered coatings remain
outside this implementation. Static BSP water classification does not track moving
liquid brush volumes. These limitations do not turn refraction into a raster copy
or screen-space effect: off-camera geometry participates in the traced paths.

### Development controls

| Setting | Default | Meaning |
| --- | ---: | --- |
| `r_rayTracing` | 0 | 2 selects this experimental mode after `vid_restart` |
| `r_pathTracingSamples` | 4 | Samples per pixel per rendered frame, 1-64 |
| `r_pathTracingBounces` | 4 | Maximum surface interactions, 1-12; 1 is direct-only |
| `r_pathTracingExposure` | 1 | Linear exposure multiplier before tone mapping, 0.01-16 |
| `r_pathTracingReference` | 0 | Cheat-protected frozen geometry/light snapshot for convergence testing; not a gameplay mode |
| `r_pathTracingDenoise` | 1 | Native reconstruction master switch; bypassed in reference mode |
| `r_pathTracingTemporal` | 1 | Camera/object-reprojected radiance history before spatial filtering |
| `r_pathTracingHistory` | 8 | Maximum temporal frame count, 1-32; moving geometry/roughness may shorten it |
| `r_pathTracingTemporalDebug` | 0 | 1 displays diffuse history, 2 reflection history: green reused world, red rejected/new world, cyan reused objects, blue rejected/new objects |
| `r_pathTracingDebug` | 0 | Cheat-protected: 0 beauty, 1 diffuse, 2 reflection/transmission, 3 visible emission, 4 mapped normals, 5 roughness/metalness, 6 dielectric classification |
| `r_pathTracingTestScene` | 0 | Cheat-protected, renderer restart: adds native diagnostic materials/geometry at X=10000; no PK3 |
| `pt_info` | command | Reference/temporal state, reset counters, object matches and material/texture counts |

Use `devmap` for reference snapshots. Setting `r_pathTracingReference 1` freezes
the submitted ray geometry/lights/material time on the following frame; it does not pause the
game simulation or hide unfinished animation. Set it back to 0 to resume updates.
Keep DLSS off for reference accumulation because its changing projection jitter
invalidates the simple fixed-camera history. Samples/bounces/exposure changes
also reset history. All shaders are compiled into the renderer DLL; no PK3 patch
or runtime shader replacement is required.

Live input always contains only the current frame's samples. Reference mode alone
uses fixed-camera accumulation; turning it off invalidates temporal history before
live reconstruction resumes. All native per-pixel storage buffers together use
320 bytes per render-resolution pixel (about 2.47 GiB at native 3840x2160), before
scene storage and DLSS images; the split channels added 128 bytes/pixel. DLSS uses
its lower internal render dimensions. Allocation is recreated on renderer/resolution
restart. Large CPU scene/pose caches use renderer-owned allocations, not the
engine's small zone heap. They are freed on shutdown/map replacement as appropriate.

The renderer export ABI is now version 9; rebuild the executable and all renderer
DLLs together. The original `refEntity_t` layout is unchanged. The new cgame trap
falls back to the old submission entry point on renderers without tracking.

### Reference regression

Copy `tests/pt_lighting.cfg` to an isolated profile's `baseq3` directory and launch
`+set fs_homepath <profile> +set r_rayTracing 2 +set r_dlss 0 +devmap q3dm6
+exec pt_lighting.cfg`. It compares a frozen, converged direct-only view against
a four-bounce view, restores live geometry/HUD/weapon, turns, and exits. The test
uses exposure 8 to make indirect-light differences visible; this is a test choice,
not a validated universal map exposure. `tests/pt_reference_check.py` compares the
two images as a coarse integration check, not a proof of physically exact output.
At 8 samples per rendered frame the reference captures accumulate approximately
1,600 samples per pixel. Allow a generous watchdog; reference mode is intentionally
not a real-time performance test. Compare images only after the script exits
successfully, since an interrupted run can leave stale captures from an older run.

`tests/pt_reconstruction.cfg` captures a converged reference plus 4-sample live
frames with filtering off/on. `tests/pt_denoise_check.py` compares their fixed-view
display-space error. This does not test motion trails, flicker or material accuracy.
`tests/pt_transitions.cfg` exercises turning with HUD/weapon, `vid_restart`, an
ultrawide resize to 1280x540, a map change and returning to the menu. Run all of
these in an isolated profile.

### Temporal reconstruction regression

`tests/pt_reprojection_check.py` tests the projection/disocclusion math, including
jitter and 4:3, 16:9, ultrawide and super-ultrawide views. It does not execute GPU
code. `tests/pt_temporal.cfg` exercises actual camera rotation/translation, a
server-issued camera cut, tracked weapon reuse and firing, using the debug
view and `pt_info` checkpoints. After a successful isolated q3dm6 run, use:

```text
python tests/pt_temporal_check.py <profile>/baseq3/screenshots --mode motion --log <profile>/baseq3/qconsole.log
```

The camera-cut screenshot is a recovery view after client snapshot delivery,
not a guaranteed capture of the first cut frame. The log checks that the cut
registered a reset and that ordinary turning did not cause a global reset.
`tests/pt_objects_materials.cfg` adds third-person walking/jumping, live versus
frozen material time, and weapon effects. `tests/pt_effects.cfg` visits a known
three-layer animated flame in q3dm1. Inspect these captures for silhouette/pose
trails, flame coverage and changing versus frozen phases; a matched-surface count
alone is not a visual-quality assertion.
After a successful flame test, run `tests/pt_effects_check.py <screenshots>` to
check that visible flame pixels change live and stabilize in the frozen reference.
The motion script uses fixed game-time steps and allows client-snapshot delivery
before checking the scripted cut, rather than assuming a fixed GPU frame rate.

`tests/pt_temporal_quality.cfg` captures an approximately 1,600-sample reference,
four one-sample spatial-only frames, four one-sample temporal+spatial frames and a
world-surface debug mask. With DLSS off, compare fresh captures using:

```text
python tests/pt_temporal_check.py <profile>/baseq3/screenshots
```

The NumPy/Pillow checker measures display-space MSE and four-frame variance on
exposed static-world pixels, excluding emitters, sky and dynamic silhouettes.
This is one fixed view, not a general reflection, motion-trail or unbiased-lighting
quality benchmark. Do not compare stale screenshots after an interrupted test.

### Rebuilding embedded ray shaders

The checked-in `Compiled/*_comp.c` arrays are linked by the normal build. To change
ray shaders, run `code/renderer_vulkan/shaders/compile-raytracing.ps1` with
`-VulkanSDK <installed SDK directory> -CC <gcc executable>`, then rebuild the game.
The script compiles for Vulkan 1.2, runs `spirv-opt -O`, validates SPIR-V and regenerates the embedded
C arrays. Add `-Raster` to also regenerate/validate all six raster vertex/fragment
shaders. The Linux shader script also covers compute shaders. No PK3 is produced.

### Frame-time regression and native performance fixes

The material expansion exposed a severe performance regression at native 1080p.
The fixes preserve samples, bounce count, lighting channels, material effects,
refraction and reconstruction:

- Load individual SSBO material properties instead of copying 3,280-byte records
  and their variable-indexed layer/modifier arrays into each shader invocation.
- Keep ray geometry, attributes, materials and lighting in device-local buffers.
  CPU assembly and hashing use cached RAM; explicit staging copies and
  transfer-to-build/compute barriers make updates visible. Material records are
  uploaded when changed. The existing completed-frame fence protects uploads.
- Cache the world BLAS and separately build moving-object and first-person
  instances. Instance masks prevent weapon-only rays from traversing the world.
  World opaque triangles use hardware opaque intersections; sky, cutouts,
  effects and dielectrics retain shader-controlled traversal. Global triangle IDs
  include instance/geometry offsets, preserving material and motion lookup.
- Visibility uses one terminate-on-blocker query, with multiplicative filters
  handled along the segment. Nonopaque geometry forbids duplicate any-hit
  invocations. Immutable texture/color stages have a compiled fast path;
  animated and other authored stages retain the general evaluator.
- Optimize and validate the embedded compute SPIR-V during regeneration.

On the RTX 5070, Windows release, q3dm6 at 1920x1080, four samples/four bounces,
DLAA + Neural Rendering, FG off: the original 59-frame static/turn test measured
629 ms median frame time and 583.376 ms in the integrator. After optimization the
same short test measured 42 ms and 29.036 ms respectively. These are local
development measurements, not a universal FPS promise.

Longer final captures (599 frames each, a longer stationary phase and camera
sweep; not the identical short-baseline view distribution):

| 1080p output, NR on, 4 samples / 4 bounces | Median frame | 95th percentile | Worst frame |
| --- | ---: | ---: | ---: |
| DLAA, native 1920x1080 tracing | 50 ms (~20 FPS) | 53 ms | 62 ms |
| DLSS Quality, 1280x720 tracing | 24 ms (~42 FPS) | 26 ms | 30 ms |

DLAA remains expensive; these fixes do not establish a smooth 60-FPS native
path-tracing budget. Saved user settings and quality defaults were not changed.
The remaining NVIDIA-internal validation issues below are not fixed by this work.

`r_pathTracingProfile 1` (cheat/development mode) reports real frame intervals and
GPU timestamps for raster, scene upload/build, integration/attribute upload,
temporal filtering, spatial filtering and post-processing. It is off by default;
it reads queries only after the existing fence, never adding a query wait.
`tests/run-pt-performance.ps1` runs `tests/pt_performance.cfg` in the isolated
`build-widescreen/rt-audit` profile, with validation disabled for timing. Use
`-Validation` for a separate correctness run. Summarize using
`python tests/pt_performance_check.py <log> --min-frames 500`; `--compare <log>`
also accepts the shorter original baseline. Do not run GPU tests concurrently.
`tests/pt_shader_storage_check.py <pathtrace.cspv> --spirv-dis <spirv-dis executable>`
guards against reintroducing the full-record shader copies.

Performance-fix regression results (2026-09-07): the final native material
fixture passed its six transport invariants, map variation, refraction and
channel-recomposition checks (1.175/255 recomposed MAE). Stock q3dm8 water and
q3dm11 glass passed, including camera IOR transitions; their test now verifies
camera positions and allows the loopback connection to finish before teleporting.
1080p DLAA motion checks passed world/object reuse, disocclusion, cuts and firing.
The animated-flame test passed (28.48/255 live change, 0.57/255 frozen change).
Restart, ultrawide resize, map/menu transitions and shadow-mode regression
completed. Synchronization validation found no additional diagnostics in these
runs, but still reported the documented NVIDIA-internal 09600/00067 errors.

### Verified development build (2026-09-06)

Windows x86-64 release build, NVIDIA GeForce RTX 5070:

- All four embedded ray/denoising shaders compiled and passed Vulkan 1.2 SPIR-V validation.
- Sampling normalization checks passed (`tests/pt_sampling_check.py`).
- The final 640x360, 1,600-sample reference pair passed the coarse lighting check:
  mean indirect-light increase 6.10/255 in the exposed-pixel mask, with 81.0% of
  those pixels brighter by more than two levels.
- The final 4-sample spatial-filter comparison reduced fixed-view display-space
  MSE from 252.19 to 51.63 (79.5%) against the 1,600-sample reference. This is one
  view, not a general quality benchmark or a temporal stability claim.
- Camera/HUD/weapon, renderer restart, q3dm6 to q3dm17, and menu return completed
  with DLSS off at 960x540 and with DLSS Quality at 1920x1080 output/1280x720 input.
- q3dm6 loaded 113 map point/spot lights; q3dm17 loaded 45. The black-sky sunlight
  boundary and ray-shaded first-person weapon were inspected in captured output.
- The existing shadow mode completed its camera/HUD/restart/menu regression.

Camera-reprojected reconstruction update, same GPU/build configuration:

- Projection/disocclusion math checks passed (three tests), as did the two
  existing sampling normalization tests.
- The 640x360 one-sample, four-frame comparison reduced mean display-space MSE
  from 84.96 to 56.91 (33.0%), and frame variance from 26.54 to 3.62 (86.4%),
  versus spatial-only filtering in the exposed static-world mask.
- Actual debug captures passed world-history reuse, dynamic exclusion,
  disocclusion, camera-cut and game-light reset checks at native 640x360 and
  DLSS Quality 1920x1080 output/1280x720 input. More than 86% of the image reused
  history during the tested turn in both runs; this is not a ghosting-quality score.
- Native 960x540 restart, ultrawide 1280x540 resize, q3dm6-to-q3dm17 and menu
  return completed with temporal history enabled. The same sequence passed
  with DLSS Quality, starting at 1920x1080 output and resizing to 1280x540 output
  (853x360 internal). This also exercises non-workgroup-aligned image bounds.
- The shadow-only camera/HUD/restart/menu test was rerun successfully against
  the updated build.

The measurements above precede the moving-object/material update and are historical,
not quality scores for the expanded material model.

Validation corrections in the moving-object/material update:

- Explicitly enable supported private-data device features required by Streamline.
- Initialize raster clip-distance varyings in all shader variants.
- Match DLSS input motion format and provide combined camera/object motion directly.
- Order NGX output clears and shader writes with correct transfer/storage barriers.
- Use image-indexed presentation semaphores; wait for the prior acquire semaphore's
  consumption before reuse, and cover the complete new-frame workload in the acquire wait.
- Queue screenshot/video/levelshot readback before presentation, while the application
  still owns the acquired image, and restore its presentation layout afterward.
- Use the official Streamline Vulkan helper declarations rather than a copied ABI.

These runs are still **not validation-clean**. The loaded NVIDIA DLSS-G presentation
component reports an internal fake-swapchain image expected in transfer-source layout
while tracked as present-source. This occurs even with frame generation switched off
and without screenshots. Its earlier intermittent internal presentation-semaphore
diagnostic also needs broader validation. The corrections above do not justify
suppressing those messages or claiming that all NVIDIA integration issues are fixed.
With Frame Generation active, the test also reproduced clear/copy hazards in
`nv.ngx.dlssg.Evaluate` and NVIDIA's DLSS-G command buffers. These are distinct
from the Super Resolution output-clear barrier fixed in our renderer.
`tests/pt_validation_check.py <validation-log>` reports these exact known internal
diagnostics and fails on other messages. A passed regression gate is explicitly
not a validation-clean result; validation output is never suppressed.

### Q2RTX architecture comparison

The user-supplied [Q2RTX source](https://github.com/NVIDIA/Q2RTX) is an architectural
reference, not an alternate renderer DLL for Quake III. No Q2RTX source or game
assets were copied in this update.

- Its [denoiser notes](https://github.com/NVIDIA/Q2RTX/blob/master/src/refresh/vkpt/shader/asvgf.glsl)
  distinguish direct diffuse, indirect diffuse and specular signals with different
  reconstruction paths. VQ3 Evolution now separates diffuse and specular channels,
  without implementing Q2RTX's additional low-frequency spherical-harmonic path.
- Its [path-tracer overview](https://github.com/NVIDIA/Q2RTX/blob/master/src/refresh/vkpt/shader/path_tracer.h)
  separates transparent effects from opaque primary visibility and stages direct,
  indirect, and reflection/refraction work. The guide traversal above adopts that
  separation principle; it does not implement Q2RTX's full transparency channel.
- Its [reflection/refraction notes](https://github.com/NVIDIA/Q2RTX/blob/master/src/refresh/vkpt/shader/checkerboard_interleave.comp)
  maintain distinct reflected/refracted surfaces for reconstruction. VQ3 Evolution
  now traces dielectrics but still reconstructs them using the first interface;
  reflected/refracted-object motion is a remaining difference.
- Its [material system](https://github.com/NVIDIA/Q2RTX#material-system) supports
  dedicated material textures and per-map overrides. VQ3 Evolution now supports
  base/normal/ORM/emissive maps and optical parameters. Artist-authored replacement
  assets and broad map-specific tuning remain separate content work.

### Split-lighting / PBR / optics verification (2026-09-06)

Reproduction uses the release build and an isolated writable game profile:

1. Copy `tests/pt_materials.cfg` into that profile's `baseq3`, then launch with
   `+set r_rayTracing 2 +set r_dlss 0 +devmap q3dm6 +exec pt_materials.cfg`.
   It enables only the native diagnostic scene, captures converged beauty and
   independent lighting/property views, then captures live reconstruction/motion.
2. Run `python tests/pt_materials_check.py <profile>/baseq3/screenshots`.
   The six analytic tests cover Fresnel, Snell, TIR, pane energy/identity,
   absorption/IOR cancellation and lobe partitioning. Image tests exercise the
   real material parser, texture uploads, integrator and output composition.
3. Run `tests/pt_split_history.cfg` in the same isolated profile and add
   `--histories` to the checker. Glossy regions must retain shorter reflection
   history while diffuse history remains reusable. Rough regions may legitimately
   share the same history cap; this is not tested using an arbitrary global mean.
4. `tests/pt_stock_optics.cfg` visits shipped `q3dm8` water above/below the surface
   and `q3dm11` glass. `pt_info` reports the material counts and camera IOR.
   `python tests/pt_stock_optics_check.py <qconsole.log> <screenshots>` verifies
   camera medium changes, nonempty output and substantial visible dielectric coverage.
   `tests/pt_map_materials.py <pak0.pk3> [maps...]` locates original material surfaces
   without modifying the archive. No source-controlled test needs a generated PK3.

RTX 5070, native 640x360 release verification:

- All four compute shaders passed Vulkan 1.2 SPIR-V validation; release build passed.
- The final fixture's identity-pane checker mismatch was 3.0% (sampling/edge noise),
  versus 19.8% for refractive thin glass, 30.1% for the glass volume and 40.0% for
  wavy water. This measures background displacement, not visual quality.
- Separate linear-light channels recompose to beauty with mean display error
  1.05/255, including JPEG quantization and differing reference sample counts.
- Normal/ORM variation and metallic/dielectric channel separation passed.
- Under rotation, 21.4% of mutually reusable world pixels had a shorter reflection
  history; rough walls correctly retained the same cap in both channels.
- The one-sample reconstruction regression reduced exposed-world display-space
  MSE from 193.36 to 155.83 (19.4%) and four-frame variance from 36.82 to 5.39
  (85.4%) versus spatial-only filtering. The mask covered 33.9% of the image.
- Stock-map transport reported IOR 1.000 above water and 1.333 underwater, with
  one actual BSP water volume; glass classification found the original glass shader.
- DLSS Quality at 1920x1080 output / 1280x720 input passed the motion, cut,
  disocclusion and firing-light checks with the new buffers (96.8% eligible-world
  reuse in the turn capture). These percentages do not establish ghosting freedom.
- DLSS stock-water/glass tests at four samples/eight bounces and 1080p completed,
  including submerged transport. Visible dielectric mask coverage was 43.4% for
  the water view and 93.9% for the glass view. The earlier 16-sample multi-map
  DLSS run exceeded its 150-second watchdog after reaching the glass map; it was
  not counted as a pass. High-sample reference validation uses the native fixture.
- DLSS renderer restart, resize from 960x540 to 1280x540, map replacement and
  return to the menu completed, exercising the new buffer lifecycle.
- After the final absorption-segment correction, the native fixture image checks,
  960x540 DLSS stock optics checks and original ray-shadow-mode smoke test passed
  again. All test-owned processes exited; no temporary game PID marker remains.

The NVIDIA-internal pacer image-layout and presentation-semaphore diagnostics still appear. Regression gates
report it as unresolved and fail on new messages; these runs are **not validation-clean**.

### Earlier moving-object/material verification (2026-09-06, before split-lighting)

Windows x86-64 release build on RTX 5070:

- All four compute shaders and six raster vertex/fragment variants compiled and
  passed Vulkan 1.2 SPIR-V validation; executable, renderer DLLs and native/QVM
  game modules rebuilt with the tracked-submission ABI.
- Five projection/object-identity/deforming-triangle math checks and two sampling
  normalization checks passed.
- Native and DLSS debug captures demonstrate tracked weapon-history reuse.
  The final DLSS Quality run (1920x1080 output, 1280x720 scene) passed camera
  turn/translation, disocclusion, cut and firing-light reset checks. In its turn
  capture, 96.8% of eligible world pixels reused history; tracked-object reuse
  covered 3.9% of the whole image. These percentages do not measure ghosting.
- Third-person walking/jumping captures show reuse on animated player geometry,
  with conservative rejection on some pose-changing surfaces. The test reported
  40-45 matched surfaces per checkpoint. Broader animation/mover testing remains.
- With the final effects-aware guide shader, the 640x360 one-sample comparison
  reduced exposed-world display-space MSE from 157.00 to 136.50 (13.1%) and
  four-frame variance from 33.74 to 4.72 (86.0%) versus spatial-only filtering.
  The world mask covered 33.7% of the image. This remains a single-view check.
- The final q3dm1 flame test passed its live/frozen animation check: mean change
  was 16.10/255 live versus 0.64/255 frozen in a 6.0% visible-flame mask. The
  layered emitter and its underlying scene were visually inspected. This does
  not validate all blend modes, transmission lighting, or particle effects.
- Native 960x540 path tracing passed restart, 1280x540 ultrawide resize, q3dm6 to
  q3dm17 and menu return. DLSS Quality with Frame Generation also completed the
  sequence and reported two presented frames when active. Shadow-mode regression
  completed without a runtime failure.
- Validation checks reported only the explicitly listed NVIDIA-internal
  diagnostics in those final runs; they did not report the repaired private-data,
  raster-varying, Super Resolution motion-format or output-clear issues.

The path-traced world must use unlit surface textures and material properties,
not multiply rays into the original baked lightmap image. It needs primary-ray
visibility, direct illumination from map emitters and game lights, multi-bounce
diffuse illumination, specular reflection, alpha-tested geometry, transmission,
and animated/off-camera objects. HUD views remain separate. First-person models,
particles, portals and fog need explicit handling rather than silent omission.

## Implementation sequence

1. Material-aware scene data: positions, normals, texture coordinates, triangle
   material IDs, surface emission, and stable world geometry. Preserve relevant
   lighting declarations from the original shader scripts.
2. Native Vulkan path integration: textured primary hits, direct-light sampling,
   diffuse/specular scattering and HDR radiance, with reproducible sample seeds
   and a converged reference mode. No dependence on screen-space visibility for
   secondary-ray hits.
3. Dynamic scene: animated models and moving brush entities, including objects
   outside the raster camera, with separate primary/reflection/shadow visibility.
4. Real-time reconstruction: disocclusion-aware history, motion vectors for
   moving objects, surface-aware denoising, exposure/tone mapping and DLSS input.
   DLSS Super Resolution or the existing NR DLL is not itself a ray denoiser.
5. Material/effect completeness: cutouts, glass/water, roughness/metallicity,
   normal maps, emitting animation, particles, weapon presentation, portals/fog,
   controls, performance budgets and regression tests.

## Required validation

- Show a textured world with baked lighting disabled, then demonstrate that a
  changing light affects direct illumination, reflected light and indirect bounce.
- Place a bright emitter around a corner: the wall must receive indirect light,
  and the effect must survive turning that emitter off screen.
- Show a moving player and door in a reflection while they are off camera.
- Compare diffuse, polished, rough and transparent material cases against a
  high-sample reference, including alpha-tested shadow and reflection visibility.
- Exercise camera cuts, HUD icons, resolution changes, map transitions, renderer
  restarts and DLSS on/off. Record actual rendering and validation failures.
- Treat unsupported materials/effects as explicit unfinished work. A compiling
  shader, a running executable or an available RTX extension is not completion.

## References

- [NVIDIA Quake II RTX](https://github.com/NVIDIA/Q2RTX): useful architectural
  reference for a path-traced game and material integration, not a drop-in Quake
  III renderer.
- [Khronos Vulkan ray tracing](https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html):
  ray queries and ray-tracing pipelines are mechanisms; either can implement a
  path integrator. Choosing a pipeline API does not itself add lighting features.
- [NVIDIA Real-time Denoising](https://github.com/NVIDIA-RTX/NRD): requires proper
  radiance, normal/roughness, depth and motion inputs from the renderer.
- [Spatiotemporal Variance-Guided Filtering](https://research.nvidia.com/labs/rtr/publication/schied2017spatiotemporal/):
  background on combining temporal radiance reconstruction and edge-aware
  filtering; the current native implementation is deliberately more limited.
- [Original Quake III map lighting](https://github.com/id-Software/Quake-III-Arena/blob/master/q3map/light.c):
  entity-light defaults, point-light scale, target/radius spotlights and sky metadata.
