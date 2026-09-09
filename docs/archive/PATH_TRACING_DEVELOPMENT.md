# Path-tracing development record: 2026-09-06 through 2026-09-08

This is an archived development log, not the current user guide. Dated results,
settings, limitations and proposed next steps describe the build tested at that
time; later work can supersede them. Local audit artifacts are intentionally not
in Git. See the [current guide](../PATH_TRACING.md), [current status](../STATUS.md)
and [RR development record](RAY_RECONSTRUCTION_DEVELOPMENT.md) for later results.
Source/test paths in backticks are relative to the repository root.

## Target and completion criteria

The requested target is a visual-overhaul mode comparable in scope to a
path-traced game renderer, not the existing directional shadow overlay.
The existing raster and ray-shadow modes remain available during development.
Do not label an intermediate implementation as finished full RTX.

## Current implementation: experimental native path tracer

Hardware-level capture setup and its safety/acceptance checks are documented in
[GPU_PROFILING.md](../GPU_PROFILING.md). Use the normal renderer and saved settings;
hardware diagnostics are not themselves a performance improvement.

### Team Arena terrain blending (2026-09-08)

Static BSP vertex upload used to replace both RGB and alpha with white. RGB
contains baked lighting and remains excluded from path-traced albedo, but alpha
is independent material data: Team Arena uses `alphaGen vertex` to blend terrain
layers. Erasing it made grass/rock transitions end abruptly at triangle edges.

World faces, patches and triangle meshes now retain their authored vertex alpha
in the existing attribute buffer. The shared material shader already interpolates
these weights and composes the layers; no shader, GPU layout, extra pass, asset
override or map-specific exception is needed. Dynamic vertex handling is unchanged.

`tests/pt_material_layers_check.py` executes the production world uploader for all
256 alpha values, then feeds a barycentric blend ramp through the actual GLSL
material composition. It checks smooth layer interpolation, opaque terrain
coverage, retained normals/UVs and unchanged baked-RGB exclusion. Native terrain
conversion also checks the vertex-dependent stages and UV-only fast-path exclusion.
Existing material-conversion and blend regressions pass; the release renderer builds.

Matched `terrain-before-20260908` / `terrain-after-20260908` runs use
`tests/pt_terrain.cfg` on Overdose (`missionpack/mpterra1`), with two fixed views,
DLAA/RR, fixed two samples, adaptive sampling off and FG/NR off. Both finish in
7.2 seconds under independent 45-second limits, with Vulkan validation enabled
and no validation messages; saved settings hashes are unchanged. Inspected captures
show the polygon-shaped boundaries replaced by smooth transitions in both views.
These are visual regression checks, not a performance benchmark.

### Layered monitor-frame lighting (2026-09-08)

Q3DM0's `textures/base_wall/comp3` monitor banks declared a surface-light
strength, but the no-additive-stage emission fallback used the final composed
color. This made the opaque silver frame emit along with the scrolling display.
The same error affected sampled area-light contributions, not just camera hits.

Surface lights without explicit additive glow stages now seed emission from
their backing layer and apply later foreground coverage/modulation to that
radiance. Opaque frames block it; their authored color remains physically lit
reflectance. Transparent/soft frame texels preserve display emission and its
original animation, including the low-alpha glass sheen in the stock textures.
Color-filter layers attenuate existing radiance in either legacy filter form.
Explicit additive glow, cutout/depth-equal holograms and single-layer lamps retain
their separate emission rules. The shared shader covers direct hits and area
light sampling; no new buffers/passes, map-name exception, exposure adjustment,
asset edit or PK3 override was added.

Verification: `pt_material_layers_check.py` executes native monitor conversion
and actual GLSL emission with solid/transparent/soft/cutout frames, animated
backing samples, color filters, camera/area-light queries and existing effects.
`pt_panel_lighting_check.py --pak <pak0.pk3>` checks the installed stock layers
and non-binary alpha masks. Environment, hologram, shell, health, fog, effects,
material-program and staged regressions pass. All 29 shader variants compile,
validate and match embedded arrays; the RR storage-layout check passes and the
release renderer is rebuilt.

The matched `panels-before-20260908` / `panels-after-20260908` Q3DM0 runs use
`tests/pt_panel_lighting.cfg`, saved graphics values, DLAA/RR, fixed two samples
and FG/NR off. They complete in 17.4 / 31.8 seconds within independent 45-second
limits. After-run Vulkan validation is clean and saved settings hashes match.
Inspected captures show both frames responding to scene lighting; their upper
rim mean RGB drops from 206.5 to 43.3/255 without changing exposure or ambient.
These are image-correctness runs, not performance comparisons. Display emission
and animation preservation are covered by the native/GLSL fixtures; the sparse
GPU captures do not independently establish the full display animation cycle.

### Q3DM0 animated hologram glow (2026-09-08)

The rotating female robot uses a cutout body plus a separate scrolling additive
environment-coordinate stage. The old material conversion discarded that stage
as a backed reflection map, leaving only the unlit-looking body. Deformed
shaders now preserve these authored glow stages, consistent with the existing
deformed power-up aura policy. Undeformed reflective coats, explicit dielectrics,
water/glass, and the health-orb/cross material rules are unchanged. Classification
uses shader semantics, not a Q3DM0 or model-name exception; no PK3 was changed.

The stock glow stage has no `depthFunc equal`, so it must also remain visible
where the body's alpha test fails. Simple deformed cutout-plus-additive programs
now mark this distinction in the existing `maps.w=3` lane. Body holes remain
non-occluding to shadow rays. Radiance hits add the glow and continue along the
same ray through the hole; native/RR guides likewise use the background surface
and mark the effect reactive. Solid body pixels retain their lit material plus
glow. The sampler keeps the original texture, environment-coordinate projection,
scroll/scale animation and model-local mapping. Environment coordinates describe
the authored animated effect here, not a request for a metallic reflection.

The existing stage blend word also retains `GLS_DEPTHFUNC_EQUAL`; its high bit
does not change blend-factor decoding. A stage explicitly requiring the body's
depth stays clipped to its coverage, while unrestricted glow can emit through
the cutouts, including during light sampling. No new material buffers, geometry
copies or full-screen passes were added.

Verification: native material and actual GLSL composition/coverage fixtures
exercise retained glow, alpha holes, solid body pixels, emitter sampling,
depth-equal masking, unchanged coats/dielectrics and health materials.
`tests/pt_hologram_check.py --pak <pak0.pk3>` confirms the installed MD3, original
shader stages and Q3DM0 rotating entity. Environment, shell, health, effect, fog,
material-program and staged regressions pass. All 29 shader variants compile,
validate and match their embedded arrays; GPU storage-layout checks pass and
the release renderer is rebuilt.

The matched `hologram-before-20260908` and `hologram-after-20260908` tests use
`tests/pt_hologram.cfg`, saved DLAA/RR, two samples, adaptive off and FG/NR off.
They completed in 15.5 and 36.6 seconds under separate 45-second watchdogs.
Saved settings hashes are unchanged and after-run Vulkan validation is clean.
Inspected front/animated captures show the restored moving glow, including
in body cutouts; 901 pixels in the front model region gained over 40/255 mean
RGB and exceeded 120/255 brightness. These captures verify this effect, not
exhaustive shader compatibility or a performance improvement.

### Console shadow penumbra controls (2026-09-08)

Two live, archived console controls set analytic light-source extent in native
path-tracing mode (`r_rayTracing 2`). Both default to **0**, preserving the
existing delta-source shadows and random sequence. No menu controls were added.

| Command | Range | Meaning |
| --- | --- | --- |
| `/r_pathTracingSunAngle 1` | 0-20 degrees | Full angular diameter of the map's authored sun; larger gives softer sunlight shadows. |
| `/r_pathTracingLightRadius 8` | 0-64 world units | Spherical source radius for map point/spot lights and dynamic game lights; larger gives wider penumbras. |

Use either command with `0` to restore that light family's point-source behavior.
They apply immediately without `vid_restart`, persist through normal settings
saves, and appear in `pt_info`. A map without an authored sun has no sunlight
for `SunAngle` to change. Emissive triangles continue sampling their real
geometry; these controls do not resize glowing textures or alter raster/legacy
ray-shadow mode. Start with small radii: very large analytic sources can extend
through nearby walls, and broader penumbras can need more temporal convergence.

`pt_penumbra.glsl` samples actual incident directions over the source's solid
angle, following the [sphere-sampling construction in PBRT](https://www.pbr-book.org/4ed/Shapes/Spheres).
Surface BRDFs and fog use those same directions and shadow queries end at the
sampled spherical source boundary. The point-source radiance normalization
retains source power rather than multiplying brightness by the enlarged area.
Inside a source, a finite two-sided spherical model replaces the exterior cone.
These analytic lights remain direct-light samples, not newly visible bulb meshes.
This is not a depth-based blur or shadow-opacity adjustment.

Original/unified/compact/staged lighting and RR use the same sampler. Finite
sources use full-support point-light selection weights so a source crossing a
surface horizon or spotlight cone is not incorrectly rejected by its center.
Changing either control resets RR/native lighting history and frozen-reference
accumulation. Two reserved light-buffer scalars carry the controls; buffer ABI,
samples per pixel, bounce count and per-selected-light shadow sample budget are
unchanged. No new full-screen passes were introduced.

Verification: `tests/pt_penumbra_check.py` executes the production GLSL sampling
math for cone bounds, unit vectors, tiny angles, sphere intersections, inside
sources, energy normalization, and a geometric blocker with tighter contact
shadows. It verifies zero-control direction/RNG identity. The production light
loops agree over 100,000 randomized hard/soft-source scenes, including 756,300
shadow rays; the map-light rejection fixture checks 1,120,000 reservoirs with
hard and finite sources. RR sampling, fog, staged, ambient and effect regressions
pass. All 29 shader variants were rebuilt/validated and match their embedded
arrays; the release renderer is rebuilt.

The isolated `penumbra-live-20260908` Q3DM0 test ran hard -> radius 8 -> sun
angle 2 -> restored hard, with live history resets and clean Vulkan validation.
It completed in 30.4 seconds under a 45-second watchdog, using saved 1080p
DLAA/RR, two samples, adaptive off, FG/NR off; saved configuration hashes were
unchanged. Captures were inspected for rendering continuity and shadow changes.
This is a control/validation check, not a performance benchmark or exhaustive
quality evaluation of every source size and map.

### Traced health-cross metal reflections (2026-09-08)

Stock health crosses now retain their yellow/gold and orange/gold pigment but
reflect the actual traced scene, not the painted environment image. Native
conversion recognizes opaque environment-base materials in
`models/powerups/health/`. A uniform-pigment stage reads only the normalized
coarsest-mip color of the original texture, reusing the shell tint sampler;
reflection projection and base UV animation no longer affect that pigment.
The existing GGX transport supplies reflections with metalness 1 and roughness
0.12. Transport and RR/native guides use the same material evaluation.

The red/orange cross's separate animated electrical overlay remains intact;
its visible moving pattern is an authored effect, not a reflection map. Outer
orb materials are unchanged. Explicit PBR/base-color/ORM replacements, alpha
overlays, deformed materials, authored emitters and unrelated environment-map
artwork retain their existing handling. This is a source-level conversion, not
a replacement PK3 or a hardcoded RGB palette. No new rays, buffers or passes
were added.

Native material fixtures cover the stock base-only and electrical-overlay
layouts, exclusions, animation and unchanged thin shells. Source/GLSL checks,
all 29 shader variants, SPIR-V validation and the release build pass.
`tests/pt_health_metal_check.py` checks the matched captures made by
`tests/pt_health_metal.cfg`. The `health-metal-before-20260908` and
`health-metal-after-20260908` Q3DM0 runs completed in 15.4 and 34.9 seconds,
respectively, under independent 45-second limits. They used matching saved
1080p DLAA/RR, flat two samples, ambient 0.05 and exposure 3.363586, with
adaptive sampling, FG and NR off. Saved configuration hashes were unchanged;
the after run had clean Vulkan validation. Inspected yellow/orange captures
show the retained metal tints and removed painted base pattern, with the
electrical overlay and shells preserved. These close-up captures are not a
performance benchmark or proof of temporal quality during extended movement.

### Console ambient fill (2026-09-08)

`r_pathTracingAmbient` is a live, archived, console-only control in path-tracing
mode. It defaults to **0** (disabled), accepts **0-2**, and needs no `vid_restart`.
Try `/r_pathTracingAmbient 0.05` for subtle fill or `0.15` for more; use `0` to
restore the original lighting. No graphics-menu controls were added.

This is optional artistic, **unoccluded diffuse fill**, not an exposure change
or a physically traced environment light. It adds neutral light to diffuse
surface response and surviving isotropic fog scatters before tone mapping,
while keeping material colors and existing path attenuation. Perfect metallic
mirrors have no diffuse fill of their own, but reflect filled surfaces; glass
similarly shows the illuminated scene through its normal transport. It does
not directly brighten sky textures, additive effects, emissive textures or HUD.
Existing physical direct/bounced lighting remains in place.

The strength occupies a reserved scalar in the existing light buffer; it adds
no rays, random samples, descriptor bindings or full-screen buffers/passes.
Changing the value resets RR/native lighting history and frozen-reference
accumulation. The default-off helper does not alter radiance or BRDF state.
`pt_info` reports the strength separately from exposure. Offline checks are
`tests/pt_ambient_check.py` and the production transport arithmetic fixture;
`tests/pt_ambient.cfg` exercises live OFF/0.15/OFF in Q3DM0 at saved quality.
The `ambient-live-20260908` validation run completed in 26.2 seconds at 1080p
DLAA/RR, two samples, adaptive off and FG/NR off. Both value changes reset
lighting history; the wall-region RGB mean changed 10.60 -> 28.24 -> 10.30/255
without changing exposure. Captures showed the filled room and player in the
mirror. Vulkan validation was clean and saved configuration hashes were
unchanged. All shader variants and the release renderer were rebuilt.

### Stock BSP mirrors (2026-09-08)

The Q3DM0 mirror was missing because the static acceleration-structure loader
excluded every `SS_PORTAL` polygon. This exposed the backing wall. The loader
now identifies planar BSP mirrors using the original `misc_portal_surface`
entity: targetless entities represent mirrors, targeted ones represent remote
cameras. Matching uses the original 64-unit plane tolerance and nearest surface
bounds to disambiguate coplanar entities. Both geometry sizing and upload use
this classification; no shader-name or map-name special case is used.

Included mirror materials default to smooth metallic reflection, retaining
explicit PBR overrides. `maps.w=2` selects mirror coating composition: start
with unit reflected radiance and apply the original stage blends. The stock
transparent-black `mirror1` base therefore preserves the reflection, while
the complementary `sfx/mirror` filter retains its authored tint. The existing
GGX transport supplies the reflected room/player; this is not a cubemap or a
raster portal image. Transport and RR/native guides share the same material.
No additional GPU buffers or full-screen passes were introduced.

`pt_world_mirror_check.py` compiles the actual classifier, parser and plane
math, including the installed Q3DM0 entity data. Material conversion/composition
fixtures exercise the actual native and GLSL functions, including coating
alpha, filters and explicit PBR. `pt_stock_mirror.cfg` captures front and angled
views at saved quality. The matched `stock-mirror-before-20260908` and
`stock-mirror-after-20260908` runs reproduced the wall and then showed the
reflected room and player in both views. The final run loaded exactly one BSP
mirror, evaluated RR, completed in 34.8 seconds, and produced no Vulkan
validation messages. Both runs preserved the saved configuration (1080p DLAA,
RR, flat two samples, adaptive off, FG/NR off). The release renderer SHA256 is
`949B4BED2E1F4B49D5494613F132E4EFC98335AA97A1E68B330B845C202FCC88`.

Scope: static planar BSP mirrors. Remote-camera portals, curved/deformed or
runtime-created mirrors are not implemented by this change. These stationary
captures do not establish ghost-free reflection reconstruction during motion.

### Fog query/cache performance follow-up (2026-09-08)

Fog lighting now shares the packed emitter geometry cache with surface lighting,
avoiding redundant triangle/vertex gathers when that existing specialization
is enabled. Free-flight fog collisions are sampled before geometry traversal
and cap its maximum distance. A closer surface still wins; first-person weapon
priority, camera near clipping and transparent-surface continuation are kept.
There is no conditional resampling after the geometry query. Tests execute the
actual GLSL query functions and compare 500,000 reference/optimized trials,
including occluded earlier fog records. Random-number ordering can change in
fog, but extinction/collision probabilities remain equivalent.

Matched 1080p DLAA/RR, flat two samples, four bounces and FG/NR off:

| Q3Tourney2 view | Before tracing / frame | After tracing / frame |
| --- | ---: | ---: |
| Grated bridge | 48.003 / 57 ms | 48.109 / 57 ms |
| Inside dense fog | 30.065 / 39 ms | 26.981 / 35 ms |

The bridge establishes **no speed gain**. The inside view improved about 10.3%
in tracing and 10.3% in engine frame time in this short pair, approximately
25.6 to 28.6 rendered FPS. This is not a whole-game result or proof of the cost
of an individual variable. The dense-fog final build also includes the mirror
material changes above. Saved settings and source configuration hashes matched;
world-region screenshot MAE was 0.252/255, with red-channel mean differing by
0.64/255 (stochastic sampling). Evidence is under the ignored audit directories
`performance-fog-opt-before-20260908`, `performance-fog-opt-after-20260908`,
`performance-fog-opt-inside-baseline-20260908` and
`performance-fog-opt-inside-final-20260908`. Dense runs completed in 14.6 and
13.9 seconds, respectively; all test processes closed automatically.

Optional shader diagnostics now report `PT_SHADER_FOG_PROFILE`: sampling,
light setup, visibility and Beer-transmittance clock shares, plus actual
collisions, surviving scatters, shadow queries and candidate-bounded segments.
Its 80-byte record contains 12 clock counters and eight event counters. Normal
pipelines compile instrumentation out. The validated bridge diagnostic measured
3.763%, 0.047%, 0.153%, and 0.721% respectively. These are invocation-local clock
shares from the **native instrumented pipeline**, not production RR GPU
milliseconds. They do not support claiming fog direct lighting dominates the
RR shader. Shader-profile ABI and all 29 embedded shader/binary pairs passed.

### Convex BSP fog volumes (2026-09-08)

Fog now participates in the native path integrator, not a screen-space overlay
or an added PK3. `pt_fog.h` loads the original BSP fog brushes and shader
`fogParms`/`q3map_surfacelight` declarations. Exact convex intervals, including
non-axial brush planes, determine extinction, volume emission and isotropic
scattering on primary and bounced paths. Shadow/light visibility also receives
Beer transmittance. Emissive triangles, sun, dynamic lights and map lights
illuminate scattering events; emitted fog radiance can light nearby surfaces
and appear in reflected/refracted paths.

The physical interpretation is homogeneous density with 99% extinction at the
authored opaque depth (`sigma_t = -log(.01)/depth`), linearized fog color as
scattering albedo, and collision emission `albedo * surfacelight/1000`.
It is deliberately not a pixel-identical reproduction of Quake III's fog lookup
curve. Emission is scored before absorption/scattering selection. Surviving
scattering paths are probability-compensated per color channel, preserving
expected radiance while avoiding lighting work for absorbed paths. This changes
variance, not the requested sample count or maximum bounce budget.

The immutable map buffer is uploaded only when dirty. Global bounds reject
unaffected rays, duplicate axial planes are removed only when they exactly
match the already-tested box bounds, and all other planes are retained.
Q3Tourney2 uses seven volumes and 20,528 bytes per device/upload buffer, with
no additional per-pixel storage. Empty maps consume no fog random samples.
Surface reconstruction guides are retained; fog-crossing pixels are marked
reactive for existing native optical-history/adaptive-sampling decisions.

The reported map was **Q3Tourney2**, corrected from Q3Tourney3. In the fixed
bridge view at saved 1920x1080 DLAA, RR on, FG/NR off, two samples, adaptive off
and four bounces, the measured medians were:

| Renderer | Tracing | Engine frame |
| --- | ---: | ---: |
| Before support (fog missing) | 33.380 ms | 44 ms |
| Initial weighted-scattering implementation | 67.070 ms | 78 ms |
| Absorption/scattering selection | 48.397 ms | 59 ms |

The bounds-only optimization did not establish a timing improvement. The final
sampling change recovered about 28% of the initial fog implementation's tracing
time, but adding real fog still costs about 15 ms in this view (roughly 23 to
17 rendered FPS versus missing fog). This is **not** a no-cost feature or a
whole-game performance claim. Bridge/inside captures show the missing medium
and colored lighting below/around the grate. Validation was loaded and clean;
the 33.5-second run completed normally and preserved saved settings.
Evidence is under `build-widescreen/rt-audit/performance-fog-tourney-*-20260908`.

The final Q3CTF3 inside/outside smoke test completed in 8.7 seconds with clean
validation and visible blue volume fog. Its outside camera is too close to a
wall for a useful scene-quality assessment; it is not broad visual acceptance.
The final no-fog Q3DM6 run completed in 15 seconds. Its screenshot matches the
pre-fog build byte-for-byte outside the FPS digits. Median tracing/frame time
was 20.547/29 ms versus 19.287/28 ms before support; an earlier intermediate run
was 18.684/27 ms. These short runs do not establish a no-fog performance gain
or exact overhead. All saved configuration hashes remained unchanged.
Final evidence: `performance-fog-stock-final-20260908` and
`performance-fog-clear-final-20260908`. All 29 compute binaries passed SPIR-V
validation and matched their embedded C arrays; the release renderer rebuilt.

`tests/pt_fog_check.py` compiles the actual GLSL math and native plane-deduplication
function: 100,000 slab/reference comparisons, overlapping-volume free-flight
statistics, analytic extinction/emission, and spectral absorption/scattering
energy checks. It also checks the compiled binding/record offsets and accepts
`--gpu-run` directories to verify quality settings, completion, fog counts and
validation. `run-pt-performance.ps1 -Map q3tourney2 -Config pt_fog_tourney.cfg`
selects the reproducible scene; retain `-Bounded -TimeoutSeconds 45 -VisibleWindow`
for the independently enforced window limit.

Limits: animated cloud-texture layers on fog boundaries are not implemented;
fog is homogeneous and isotropic, without dedicated volumetric motion guides
or an importance sampler for volume emitters. Dense/moving fog, arbitrary mod
materials and combined glass/water/fog motion need broader image-quality checks.
Earlier sections that list fog as wholly missing describe the older renderer.

### NVIDIA Ray Reconstruction with DLAA (2026-09-08)

An optional native HDR reconstruction route now calls NVIDIA's public Streamline
DLSS-RR feature, replacing the built-in filters rather than stacking denoisers.
The new Graphics Options toggle is `r_dlssRayReconstruction`; use `r_dlss 5` for
native DLAA. RR now defaults on where supported; saved explicit opt-outs remain.
Sampling now defaults to a flat 2 samples per pixel (`r_pathTracingSamples 2`,
`r_pathTracingAdaptive 0`). The optional RR adaptive sampler treats the requested
count as a ceiling and can halve it on stable diffuse surfaces; bounces and DLAA
resolution stay fixed. Temporal/spatial map-light reuse
is controlled separately with `r_pathTracingLightReuse` (default 1).
At matched 1080p DLAA, four bounces, FG/NR off, q3dm6 measured 22.7 FPS with native
four-sample reconstruction, 22.7 with RR/four samples, 35.7 with RR/two samples,
and 58.8 with RR/one sample. The lower-sample results are an explicit quality
trade-off, not a same-quality shader optimization or whole-game average.
Input contracts, runtime requirements, validation and evidence are in
[RAY_RECONSTRUCTION.md](../RAY_RECONSTRUCTION.md).

The subsequent RR sampling pass measured 22.2 -> 25.6 rendered FPS at the user's
four-sample ceiling, four bounces and DLAA (FG/NR off) in the same stationary
q3dm6 view. Adaptive sampling supplies most of that gain; rapid camera motion
restores full sampling. This is not a claim of a map-wide or moving-camera gain.

### Workgroup register-budget improvement (2026-09-08)

The compact integrator now uses a hardware-checked NVIDIA workgroup specialization
(8x128, or 8x64 when device limits require it), with 8x64/8x8 creation fallbacks.
Other GPU vendors retain 8x8. It preserves the per-pixel shader calculations,
sampling, bounces, materials and reconstruction; it does not use the rejected
function-outlining or split-integrator prototypes.

At saved 1080p DLAA, 4 samples/4 bounces, with FG and NR off, the verified q3dm6
comparison improved tracing from 55.540 to 35.870 ms and real-frame medians from
62 to 43 ms (about 16.1 to 23.3 FPS, +44%). Screenshots were byte-identical.
The 8x64 intermediate result was separately reproduced. These are bounded,
stationary-scene measurements, not a claim that performance is now sufficient
everywhere. Full evidence, rejected experiments and the separate NR cost test:
[GPU_PERFORMANCE_ANALYSIS.md](../GPU_PERFORMANCE_ANALYSIS.md).

Performance tests now force Frame Generation off in isolated profiles and record
that override; the user's saved settings are never rewritten. In PowerShell 7,
`run-pt-performance.ps1 -Bounded -TimeoutSeconds 45 -VisibleWindow` uses an
independent job owner to close only its test process, including on interruption.

### Earlier performance evidence and rejected split-integrator hypothesis (2026-09-08)

The combined material cache, packed emitter geometry and compact transport
changes were measured together at the saved 1920x1080 DLAA, four-sample,
four-bounce settings. A/B/B/A and reverse B/A/A/B completed normally in 22.6
and 22.8 seconds, with 41 settled samples per phase. Tracing fell 6.41% and
6.22%, approximately 59.6 to 55.9 ms; engine-frame medians fell from 72–73
to 68–69 ms. These are measured improvements, **not a satisfactory final frame
rate or a diagnosis of every remaining hardware bottleneck**. Evidence:
`pt-perf-optimization-pass-combined-{a,b}-20260908.log`; configs
`pt_optimization_pass{,_confirm}.cfg`. Saved settings hashes were unchanged.

An optional staged prototype separates surface tracing/materials from direct
lighting/continuation. It preserves serial samples, the complete RNG stream,
all optical/transparent events, texture footprints, and four radiance outputs.
Its additional descriptor set has a 384-byte state per active pixel, with a
separate pipeline layout compatible with the original set 0 and push constants.
Allocation/creation failure falls back to the original full-quality renderer.
`r_pathTracingStaged` remains **0 by default**: 1 is diagnostic staged rendering;
2 compares both renderers on the same frame before denoising (reference history
must be off). Neither the staged pipeline nor its buffers are created normally.
`r_pathTracingStagedRows` is latched: 0 uses the full frame, positive values use
bands rounded to eight rows. This changes working storage, not resolution.

The first paired GPU run compared 24,883,200 pixels in all four raw radiance
channels, including a camera change: zero differing outputs, zero nonfinite
outputs. The validation layer was confirmed loaded, its log was empty, and
shutdown completed normally after 11.5 seconds. This covers the tested scene,
not every material/map. Evidence: `pt-perf-staged-paired-validation-a-20260908.log`
and its `.run.json`; the corresponding isolated profile contains validation
logs and screenshots. `pt_staged_check.py --log <log>` verifies these counters.

Correctness did **not** establish speed: the uninstrumented full-frame staged
A/B/B/A comparison completed in 25.1 seconds and was **57.34% slower** in tracing
(reference 57.45 ms, staged roughly 90 ms; engine frame 70 vs 103–104 ms).
Evidence: `pt-perf-staged-transport-a-20260908.log`. No gain is claimed and the
prototype is not enabled for regular gameplay.

After this failure, per-dispatch GPU timestamps were added to test the suspected
cause instead of assuming fewer registers or a smaller buffer meant improvement.
`r_pathTracingStagedProfile 1` with `r_pathTracingProfile 1` reports exclusive
stage times and gaps. Results are read after the existing frame fence without
waiting; all instrumented whole-frame rows are marked `PT_PROFILE_DIAGNOSTIC`.
These timestamps perturb scheduling and are **not FPS acceptance results**.

| Median diagnostic time | Full-frame state | 32-row state |
| --- | ---: | ---: |
| Initialization | 4.082 ms | 1.084 ms |
| Surface tracing/materials | 42.314 ms | 40.062 ms |
| Direct lighting/continuation | 36.818 ms | 42.426 ms |
| Accumulation/sample advance | 8.222 ms | 4.081 ms |
| Inter-dispatch gaps | 0.007 ms | 0.243 ms |
| Total span | 91.511 ms | 88.072 ms |

Each diagnostic has 22 settled frames, completed normally in 18.0 / 15.3 seconds,
and preserved saved settings. The working state fell from 796,262,400 to
23,592,960 bytes; dispatches rose from 37 to 1,258. Native command recording was
approximately 0 / 1 ms at the timer's resolution. Evidence:
`pt-perf-staged-stage-profile-{full,bands}-20260908.log`; use
`pt_staged_check.py --profile-log <log>`. The smaller working buffer reduced
initialization/accumulation but did not solve the slower surface/lighting stages.
It is not evidence that the normal renderer is purely memory-bound, nor that
CPU dispatch overhead explains its poor frame rate. Hardware execution/cache
stall attribution remains unestablished; do not present that hypothesis as a
diagnosed cause or this prototype as a performance fix.

The final normal-build validation (staged requested/active both 0, no staged
allocation) completed in 9.6 seconds with the validation layer loaded, an empty
validation log, cache transitions and camera motion completed, and the original
settings hash unchanged. Evidence:
`pt-perf-optimization-pass-final-validation-20260908.{log,run.json}`. The explicit
offline regression suite passed 213 tests; the four experimental stage binaries
also passed SPIR-V validation and the 384-byte state ABI checks. This validates
the tested normal path, not a speed benefit for the disabled prototype.

### Compact transport and bounded-test environment

`r_pathTracingCompactTransport 1` is now the default for the normal optimized
lighting configuration (mode 57: BRDF reuse, packed emitters, unified lights,
material cache). Four radiance outputs remain separate, but mutually exclusive
path weights use two RGB vectors plus a first-event class instead of four RGB
vectors. There are no new buffers, changed random draws, removed lights, or
sample/bounce reductions. Other diagnostic configurations retain their full
original integrators. Pipeline creation failure retains the original; startup
compiles only the selected integrator, not an unused reference as well.

Two normal-host comparisons at the saved 1920x1080 DLAA, four-sample/four-bounce
settings completed in 20.9 and 21.5 seconds with 41 settled samples per phase.
A/B/B/A measured 1.40% less tracing time; reverse B/A/A/B measured 1.30% less,
approximately 56.7 to 55.9 ms. Engine-frame medians improved by roughly 1 ms.
This is another small gain, not completion of the overall performance work.
Evidence: `pt-perf-compact-transport-normal-{a,b}-20260908.log` in the ignored
audit directory. Use `pt_compact_transport.cfg` / `_confirm.cfg` and
`pt_compact_transport_check.py --log <log> [--reverse]`.

The production arithmetic fixture checks 2.4 million events under each of
strict and fast math, preserving all four radiance channels and roulette
weights (strict results exact; fast-math differences bounded). Native fixtures
cover disabled/incompatible modes, cached startup, partial creation failures,
fallback and resource cleanup.

**Run GPU checks with normal host driver-cache and Windows service access.**
Filesystem-restricted launches repeatedly recompiled the same shader for about
5.9 seconds and stalled at shutdown. The bounded stack capture found NGX
shutdown waiting in `NvTelemetryAPI64::UninitializeTelemetry`, with its worker
in Windows file access through `NvTelemetryBridge64`. The identical warm-up
outside those restrictions exited normally in 9.9 seconds; the existing shader
loaded in 3 ms. The subsequent comparisons also exited normally. Do not patch
or skip NVIDIA shutdown, disable its services, or count timed-out runs as gains.
Keep the 45-second watchdog and isolated saved-settings copy. All restricted
compact runs and the debug capture are diagnostic-only, not FPS results.

The enabled compact-weight default then passed a 9.4-second normal-host Vulkan
validation run: the validation layer was confirmed loaded, the log was empty,
cache switches and camera movement completed, and shutdown preserved settings.
Evidence: `performance-compact-transport-default-validation-20260908/`.

Further per-ray scene-reference experiments did **not** improve performance.
The combined medium/decal version was 0.87% slower, medium-stack references
alone were 0.15% slower with unchanged frame medians, and decal references alone
were 0.75% slower. Each completed four-phase test had 41 settled samples per
phase, unchanged quality and normal exit in 26.5–26.7 seconds. Their runtime
switch, wrapper and generated shader payloads were removed; compile-only
`tests/pt_compact_{records,media,decals}.comp` and the exact-value/order fixtures
remain. Smaller driver-reported private storage did not translate into faster
frames. Logs are `pt-perf-compact-{records,media,decals}-a-20260908.log`.
New test settings manifests also record executable/renderer SHA-256 hashes.

### Current FG-off presentation lifecycle and packed-light default

The renderer now follows Streamline's bundled DLSS-G programming guide section
18: after querying hardware support on a fresh device, but before creating the
surface/swapchain, it calls `slSetFeatureLoaded(kFeatureDLSS_G, false)` when FG
is off. This avoids creating the unused off-screen proxy and presentation queue.
DLAA, NR and Reflex remain loaded. FG capability remains available in the menu;
the existing latched FG setting restarts the renderer when changed. Requested
FG keeps the loaded path; failed/unavailable unhook calls retain valid original
interfaces. No hooks change underneath a live swapchain or pending frame.

This is a correctness/lifecycle fix, **not a measured FPS gain** in the tested
heavy scene: separate old/new captures both measured 69 ms median frames and
75 ms p95, with tracing 56.171/56.136 ms. Each had 93 settled samples, used the
same saved settings, and closed in 13.8/13.7 seconds. The original path is
reproducible with diagnostic `r_dlssFGIdleHooks 1` at startup; the runner's
`-KeepIdleFrameGenerationHooks` records that override without changing quality.
Use `tests/pt_idle_fg_hooks.cfg` and `tests/pt_idle_fg_hooks_check.py`.

The subsequent **14.0-second FG-off validation run was clean**: the runner
confirmed `VkLayer_khronos_validation.dll` loaded in its own test process,
`validation.log` was empty (zero warnings/errors), cache transitions and camera
markers completed, DLAA/custom NR stayed active, and normal shutdown preserved
the saved configuration hash. The two earlier Streamline presentation VUIDs
did not recur. This covers the tested FG-off path, not untested FG-on operation
or a broad-map visual/performance guarantee. Evidence:
`pt-perf-idle-fg-hooks-{reference-a,native-a}-20260908.log` and
`performance-idle-fg-hooks-native-validation-20260908/`, under the ignored audit
directory, with their run/settings records. The first reference's older summary
does not recognize the new completion marker; the dedicated parser accepts its
full completed log. Future summaries recognize both new scenario markers.

Packed emitter geometry is also now enabled by default
(`r_pathTracingEmitterGeometry 1`) after two completed comparisons on top of
the material cache: tracing improved **2.22% / 2.10%**, approximately one
millisecond, with engine-frame medians around 71 → 70 ms. Runs closed in
27.4/22.0 seconds with 41 settled samples per phase and unchanged settings.
The 300,000-case production packing/shader fixture remains bit-identical under
both strict and fast math. Evidence: `pt-perf-cached-emitters-user-settings-
{a,b}-20260908.log`; scenarios `pt_cached_emitters.cfg` and its `_confirm`
counterpart (reverse parser order for the latter). These modest gains do not
make the remaining approximately 56 ms of tracing cost acceptable or finished.

### Material animation cache: confirmed small gain, enabled by default

The next performance pass used short game windows (all under 45 seconds) and
freshly copied saved 1920x1080 DLAA, four-sample/four-bounce, 16x filtering,
exposure 4 and custom NR settings, with Frame Generation off. Saved configuration
hashes were unchanged after every completed run. This remains a small gain,
not a claim that the renderer's overall poor performance is fixed.

`r_pathTracingMaterialCache` now defaults to **1**. A tiny compute pass calculates eligible material animation values
once per frame. It uses an unused fourth texture-modifier slot, so it adds no
buffers or descriptors and does not change the 3312/352-byte material/layer
layouts. Original UV transform order, anisotropic derivatives, texture samples,
lighting rays, samples and bounces are retained. Entity-specific time/scroll,
position-dependent stages, four-modifier stages, and reflective-shell pigment
sampling retain the original material program. Detailed shader-clock profiling
also uses the original program and reports the cache inactive.

The cache-enabled lighting shader is a separately compiled variant (pipeline
bit 32); disabling it restores the original shader with no cache branches.
Switches re-upload original material records, and GPU cache writes are ordered
before shading with a compute write-to-read barrier. Creation failure retains
the original material evaluation; no null compute pipeline is dispatched.

| Separate-shader comparison | Median tracing, original → cache | Median engine frame | Runtime |
| --- | --- | --- | --- |
| A/B/B/A | 60.28 → 57.81 ms (−4.09%) | 73 → 71 ms | 22.8 s |
| B/A/A/B | 60.01 → 57.83 ms (−3.64%) | 73 → 71 ms | 22.3 s |

Both runs had 41 settled timing samples per phase and passed completion,
active-path-tracing and initial-camera checks. The baseline driver report
returned to the previous 580,480-byte executable. Evidence is under ignored
`build-widescreen/rt-audit/pt-perf-material-cache-separate-{a,b}-20260908.log`,
with run metadata, isolated settings and screenshots. Use
`tests/pt_material_cache.cfg` and `tests/pt_material_cache_confirm.cfg` to
reproduce; parse the latter with `--reverse`.

Rejected experiments from this pass must not be counted as gains:

- Concurrent four-sample scheduling was **3.93% slower** (27.3-second run).
  Its native switch, build wrappers and eight generated runtime shader files
  were removed. The compile-only prototype and offline checks remain.
- Combining the cache and original UV programs into one shader loop was slower
  overall (79–81 ms frames). That shader was discarded, not enabled.
- The first cache run's apparent 8.12% gain compared against a shader with cache
  branches even while disabled. The separate-shader results above are the valid
  comparison against the pre-existing renderer; do not advertise the 8% figure.

The CPU fixture executes 200,000 production modifier chains at each of `-O2`
and `-O3 -ffast-math`. Strict results have identical UVs/derivatives; fast-math
permits 1e-5 absolute + 8e-6 relative UV rounding (maximum absolute 0.0078125 at
large stretched coordinates), with identical derivatives. These checks do not
replace GPU image comparison. Native pipeline lifecycle coverage is now
21,051,072 checks across 64 cached modes, including every relevant failure-chain
combination and unsupported diagnostic-layout fallback.

A separate 16.0-second Vulkan synchronization-validation run completed cache
off/on transitions and camera movement, with both status snapshots reporting
active PT/cache and the saved settings hash preserved. No material-cache buffer
or synchronization error was reported. **Overall validation is not clean:** the
pre-existing Streamline presentation errors `VUID-vkCmdDraw-None-09600` and
`VUID-vkQueueSubmit-pSignalSemaphores-00067` recurred on named `nv.sl.dlss_g`
resources. Both categories also occur in the earlier `validation-after.log`.
Their root cause remains unresolved; this is not a full renderer validation
pass. The stationary/moved screenshots were inspected, but the moved view faces
a dark nearby wall and does not establish broad material/motion quality.
Evidence: `performance-material-cache-validation-20260908/validation.log`,
its isolated game log/settings and the matching `pt-perf-*.run.json`.
Validation output now goes to each isolated test folder instead of overwriting
the earlier global audit log.

### Unified light loop: repeated GPU gain, enabled by default

Two further authorized comparisons completed at the same saved settings below,
with reversed ordering in the second run. Each phase had 41 settled samples
after discarding transition edges; completion markers, active PT, and initial
camera checks passed. The original and candidate ran in the same process.

| Run | Median tracing, original → loop | Median engine frame, original → loop | Runtime |
| --- | --- | --- | --- |
| A/B/B/A | 62.514 → 58.510 ms (−6.41%) | 76 → 72 ms | 27.0 s |
| B/A/A/B | 62.226 → 59.376 ms (−4.58%) | 75 → 72 ms | 22.8 s |

Pooled engine-frame medians were 75 → 72 ms: approximately 13.3 → 13.9
rendered FPS with Frame Generation off, **not** a large overall performance
fix. Both processes exited normally before their 45-second limits; source
configuration hashes were identical between runs and unchanged afterward.
Resolution, DLAA, samples, bounces, 16x filtering, exposure and custom NR were
not reduced. The second pair of screenshots was inspected: scene/material
appearance remained recognizable without an obvious new texture failure, but
the different animation/view states are not a pixel-identical image test.
Broader maps, motion, optics, FG-on performance, and full Vulkan/NVIDIA runtime
validation remain unverified by these short timing runs.

`r_pathTracingLightLoop` now defaults to **1** in the rebuilt release renderer.
It selects the already tested compiled variant; `0` retains the original for
regression comparison, and pipeline creation failure retains the existing safe
fallback. `pt_info` reports requested and active loop states. All 181 focused
tests pass, including the two scenario-order checks, alongside the existing
21,003,520 native lifecycle checks. No additional game launch was needed to
rebuild after changing the default and adding the status line.

Evidence: `build-widescreen/rt-audit/pt-perf-light-loop-user-settings-a-20260908.log`
and `pt-perf-light-loop-user-settings-b-20260908.log`, with matching `.run.json`,
isolated settings manifests and screenshots. Reproduce with
`tests/pt_light_loop.cfg` and `tests/pt_light_loop_confirm.cfg`; use `--reverse`
when parsing the latter with `tests/pt_material_program_check.py`.

### Driver compilation diagnosis and unified light loop (2026-09-07, later pass)

**Performance is not fixed.** The two subsequently approved 45-second runs
have now both been used. They used freshly copied saved settings: 1920x1080
DLAA, Frame Generation **off**, Reflex 2, four samples/four bounces, 16x
filtering, exposure 4, and custom Neural Rendering. The user's settings had
returned to `r_rayTracing 2` before these runs; no renderer override was used.
Both runs exited normally and preserved the saved-configuration hash.

The optional `r_pathTracingPipelineStats 1` diagnostic enables
`VK_KHR_pipeline_executable_properties` statistics at pipeline creation; its
default is 0. `tests/pt_compile_stats.c` also compiles the same integrator layout
without a window, command buffers, queue submissions, or rendered workload.
Its baseline matches the in-game RTX 5070 compiler report: 255 temporary
registers, 754,304 bytes of executable code, and 12,288 bytes of shared memory.
These are compiler resource reports, **not** measured occupancy, stalls, or
evidence of a hardware ceiling. The driver's raw local-memory values have an
implausible high-word component and are retained uninterpreted in the logs;
do not report them as literal per-thread memory use. Nsight's hardware-counter
query reported insufficient privilege; no counter policy was changed.

A selective `DontInline` experiment reduced registers to 128 and executable
code to 285,312 bytes, but the completed same-process A/B/B/A run decisively
rejected it: reference tracing medians were 62.655/62.876 ms, versus
111.622/111.411 ms for the candidate (**77.57% slower** pooled). Median engine
frames were 76 ms versus 124 ms. Each phase had 41 settled frames after edge
discard; total runtime was 31.0 seconds, normal exit. The snapshots retained
the visible scene/materials, but this is not broad visual validation. That
variant and its switch were removed from the runtime build, not enabled.
The compile-only experiment remains reproducible with
`tools/pt-shader-variant.ps1` and `tests/pt_shared_functions_check.py`.

The initially default-off candidate, `r_pathTracingLightLoop 1`, consolidates the
four repeated lighting paths into one inlined visibility/BRDF site. Emissive,
sun, every dynamic light, and the existing map-light reservoir retain their
original order, formulas, MIS weights, shadow distances, and RNG sequence.
It does not reduce rays, samples, bounces, texture filtering, or effects, and
adds no image buffers. It uses pipeline bit 16 with a matching optional clock
variant; failure falls back to the same previously selected lower mode.
This is a different implementation from the rejected `DontInline` experiment.
The original BRDF shader binary remained byte-identical after rebuilding.

The native fixture executes both production light-loop bodies across 100,000
scenes per compiler configuration, covering empty light sets, 0-32 dynamic
lights, 0-1024 map lights, occlusion/colored transmission, rejected near lights,
and both existing emitter/cull variants. Strict `-O2` produced bit-identical
762,297 shadow-ray records and 653,020 split-BRDF/light contributions. Under
`-O3 -ffast-math`, counts/order/RNG remain exact; numerical checks allow ordinary
reassociation/reciprocal rounding (rays: 1e-6 absolute + 2e-6 relative;
contributions: 1e-7 absolute + 1e-4 relative). The maximum ray-component absolute
difference was 3.815e-6. Scene helpers are deterministic CPU mocks; these tests
do not replace actual ray traversal or visual comparison.

The new loop passed SPIR-V/Vulkan compile validation and reported 580,480 bytes
of driver code, still 255 registers. At that checkpoint there was no GPU timing
or visual result; the subsequent measurements are recorded above. The release
build and 179 focused tests passed at that point. Native lifecycle coverage
includes all 32 modes and 21,003,520 selection/failure/restart checks. The
prepared `tests/pt_light_loop.cfg` comparison preserved saved graphics; the user
then authorized the two further visible runs now completed above.

Evidence under ignored `build-widescreen/rt-audit/`:
`pt-perf-compiler-profile-20260907.log`,
`pt-perf-shared-functions-user-settings-20260908.log` and their metadata,
`compile-shared-options.log`, `compile-shared-full.log`, and
`compile-light-loop.log`. The shared-functions run's filename date is a label;
its local execution timestamp was September 7. Completed compiler-profile
markers are now recognized by the diagnostic summary parser as well.

### Earlier same-day performance experiments: no additional verified FPS gain

The saved-settings baseline is 1920x1080 DLAA, Frame Generation, Reflex 2,
four samples / four bounces, 16x filtering, exposure 4 and the user's custom
Neural Rendering controls. None of those were lowered for this pass. The
previously validated BRDF reuse remains enabled. The additional candidates
below are **not enabled by default** and must not be advertised as FPS gains.

The shader-clock diagnostic now separates emitter work, map-light proposals,
BRDF evaluation, continuation and light setup, retaining aggregate lighting,
queries, materials and other categories. Records have eight tick fields plus
four counters (48 bytes). Normal shaders contain no clock instrumentation.
The completed detailed run reported approximately 28.1% material and 51.9%
lighting clock shares, including 17.4% emitter work, 12.6% proposals and 16.0%
continuation. These are relative instrumented clock estimates, not GPU stage
milliseconds or hardware-utilization measurements; compiler scheduling can
move arithmetic across diagnostic boundaries.

`r_pathTracingAliasPDF 1` copies an alias target's existing probability into
the previously unused fourth float of each alias-table entry. Sampling can
then use one table read instead of two dependent reads, with the same selected
light and PDF. Native/GLSL tests cover 7,961,375 cases under each of `-O2` and
`-O3 -ffast-math`. The completed A/B/B/A and longer reversed B/A/A/B runs showed
only -0.78% and -0.50% tracing-time changes, with no convincing median frame-time
gain. The default stays **0**. Reversed log parsing still requires all four
ordered phases, the completion marker, camera verification and at least 40
settled samples per phase.

An empty-decal specialization was also tested, then removed. The real scene
contained four attached decal triangles, so the specialization never activated.
That 37.4-second run measured the same full renderer in every phase and is
**not an optimization comparison**. Disabling decals to make it activate is
not an acceptable test of the user's settings. Full decal blending, sorting
and the 16-layer limit are unchanged.

The current source has an experimental packed-emitter candidate,
`r_pathTracingEmitterGeometry 0` by default. Each emitter has exact copies of
its three submitted world-space positions and declared intensity. Normals,
sampled positions and PDFs retain the original shader arithmetic. The CDF,
random draws, material evaluation, visibility, rays and bounces are unchanged.
The appended payload is 384 KiB, independent of image resolution; all existing
light-table offsets remain intact. Animated emitters rebuild with the current
submitted geometry. The native/GLSL fixture covers 300,000 animated, planar,
degenerate and boundary-sample cases per compiler configuration, checking
bit-identical targets, normals, intensity and PDFs.

The initial **dynamic-branch** packed-emitter experiment hit its 45-second
watchdog before the completion marker and is diagnostic only. Its partial
timings do not establish a gain, and both branches were slower than the earlier
baseline across runs. That implementation is superseded in source by Vulkan
specialization constant 2 and cached pipeline bit 8. The normal variant removes
packed-data accesses at compilation, not with a per-pixel runtime switch.
Both normal and instrumented pipelines use the same specialization mapping.
All 16 mode/failure-mask combinations, partial handles, repeated selection and
shutdown are covered by 16,793,216 native mocked-Vulkan checks. The new variant
has **not yet had a completed GPU performance test**. It remains disabled.

Current checks: the corrected release renderer builds, 174 focused regression
tests pass, and all eight shader binaries compile, optimize and validate.
Frozen-specialization checks verify zero packed-geometry access chains in all
four normal variants and four such accesses in each enabled variant. A broad
discovery command additionally found
two standalone scripts that are not unittest-importable and two failures in
the separate texture-upscaler tests (JSON serialization); that tool was not
modified by this performance pass. The renderer rebuild was deferred while a
separately started game was running, then completed after it closed. Its loaded
files were not overwritten while in use.

Run evidence is kept under ignored `build-widescreen/rt-audit/`:
`pt-perf-lighting-detail-user-settings-20260907.log`,
`pt-perf-alias-pdf-user-settings-20260907.log`,
`pt-perf-alias-pdf-confirm-user-settings-20260907.log`,
`pt-perf-empty-decals-user-settings-20260907.log` and
`pt-perf-emitter-geometry-user-settings-20260907.log` with runner metadata.
Both initial additional approved GPU tests were used. The user subsequently
approved two more 45-second runs, now accounted for in the later-pass section
above. After a separate user game session the saved configuration temporarily
changed to ray-shadow mode (`r_rayTracing 1`) with Frame Generation off; it
returned to path tracing before the later tests. The runner now offers an
explicit `-EnablePathTracing` override for isolated PT diagnosis, recorded in
the manifest and never applied to the source settings; it was not needed in
those actual runs. Every test preserved its source configuration hash.

### Selected-integrator startup: runtime checked (2026-09-07)

The former startup path unconditionally created the original path-tracing
pipeline, then lazily created the default-enabled BRDF-reuse pipeline on the
first map frame. The original was normally unused. Initialization now creates
the **selected** integrator through the same mode cache used during rendering;
the original is compiled only when requested or needed for fallback. Normal
BRDF-reuse startup therefore makes one integrator pipeline-creation call, not
two. The map-light A/B scenario needs only reuse and reuse+cull, not a third
unused original pipeline. This removes redundant work without introducing a
disk cache, changing shader binaries, reducing quality, or adding image buffers.

The original is cache mode 0, not a separate eagerly constructed handle.
Combined-candidate failure still tries proven reuse, then the original. If a
debug switch and its fallback both fail after startup, the last valid pipeline
is retained; a fresh startup with no valid pipeline fails through the existing
cleanup. Failed modes are not recompiled every frame. Every cached mode,
including mode 0, is released and cleared before the shared layout on shutdown.

`tests/pt_pipeline_lifecycle_check.py --cc <gcc>` executes the **actual native
selection/creation/destruction functions** against mocked Vulkan calls, using
real Vulkan structures and specialization data. Its 5,024 checks cover every
requested mode and failure-mask combination, partial pipeline handles, shader
module failure, repeated frame selection, the A/B mode sequence, invalid modes,
failed switches, repeated shutdown and restart. The fixture performs no GPU
work. Release build and 162 regression tests pass.

The newly authorized saved-settings run completed and closed normally in
**25.4 seconds** under its 45-second watchdog. It created only modes 1 and 3
(2 ms and 3 ms respectively), never the unused original/mode 0. All four A/B
phases completed with 41 settled samples each; the BRDF-reuse mode remained
active throughout. These short creation times are consistent with a warm
driver cache. This validates the removed creation call and runtime switching,
**not** a cold-start speedup or an explanation of every previous timeout.
The user's saved configuration hash remained unchanged. Runtime confirmed
1920x1080 DLAA, Frame Generation, Reflex 2 and custom NR values; four samples /
four bounces, 16x filtering and other saved quality settings were preserved.

The bounded performance runner now saves a diagnostic `.run.json` alongside
each fresh log: elapsed time, watchdog/exit status, raw phase counts, completed
pipeline CPU compilation durations, interrupted pipeline modes and saved-config
hash preservation. Old logs without compilation markers report no timing data
rather than guessed startup cost. `tests/pt_run_summary_check.ps1` tests this
reporting without starting the game. These summaries are **diagnostic only**;
the full A/B/B/A completion and settled-sample requirements remain unchanged.
The early map-light candidate remains off; proven BRDF reuse remains on.
Evidence: `build-widescreen/rt-audit/pt-perf-selected-startup-map-light-user-settings-20260907.log`
and the adjacent `.run.json`; isolated settings are in the matching
`performance-selected-startup-map-light-user-settings-20260907/settings.json`.
No second test was launched under this approval. This run did not enable the
Vulkan validation layer or perform a dedicated visual-quality comparison.

### Early map-light rejection: rejected for normal use, disabled (2026-09-07)

The completed follow-up comparison above now establishes that this candidate
is slower in the tested saved-settings scene. It remains **default 0**; do not
enable it as an optimization. Both candidate phases were slower than both
reference phases, despite some reference-endpoint drift:

| Same-process phase | Settled frames | Median engine frame | Median GPU tracing |
| --- | ---: | ---: | ---: |
| BRDF reuse A, cull off | 41 | 75 ms | 61.367 ms |
| BRDF reuse + cull A | 41 | 78 ms | 64.336 ms |
| BRDF reuse + cull B | 41 | 78 ms | 64.320 ms |
| BRDF reuse B, cull off | 41 | 74 ms | 60.090 ms |

Combined tracing median increased **6.59%**. These are GPU/engine timings, not
Frame Generation's displayed FPS. Fewer synthetic attenuation calls did not
translate to a GPU improvement. The result does not isolate the hardware cause
(for example, divergent execution or register pressure); no such cause is
claimed from CPU tests or aggregate timings alone. The debug path and its tests
remain available for reproducibility, but the shipping/default path does not
select it. Shader math and image-quality settings were not changed in the
startup fix.

Initial implementation and the earlier incomplete test are recorded below.

`r_pathTracingMapLightCull` defaults to **0** and is cheat/debug-only, not
archived. The candidate moves the existing geometric back-face rejection ahead
of light-color/cone reads and attenuation. It does not alter the rejection rule,
eight-candidate budget, alias proposals, random draw order, reservoir weights,
selected-light evaluation, samples, bounces or reconstruction. There is no added
GPU image/buffer or descriptor. This is **not an accepted performance gain**.

Vulkan specialization constant 0 selects the candidate when creating a pipeline,
not on each pixel. The lazy native cache has independent bits for proven BRDF
reuse and experimental map-light rejection. Mode 0 selects the base pipeline;
failure of the combined candidate falls back to BRDF reuse, then the base if
necessary. Cached pipelines are destroyed at renderer shutdown. Shader-clock
diagnostics select the same specialization. `pt_info` and `PT_MAP_LIGHT_MODE`
report the actual active state, including fallback. The new constant changes
the integrator SPIR-V; the earlier byte-identity observation below is historical.

Offline checks execute the production GLSL attenuation, alias proposal and
reservoir loop for **1,120,000 comparisons per compiler configuration** (`-O2`
and `-O3 -ffast-math`). On/off selected lights, total/selected weights, random
draw counts and final RNG state are bit-identical within each configuration.
Cases include 0--1024 lights, nonuniform alias distributions, front/back/coplanar
lights, tiny distances, black lights, spots and shading/geometric disagreement.
The test fixtures skip roughly two thirds of attenuation calls; that synthetic
operation count does **not** establish a GPU benefit. All four integrator /
diagnostic modules have both specializations frozen, optimized and validated.
Release build, all eight shaders, 158 regression tests, BRDF math fixtures,
material/environment/sky fixtures and storage-layout checks pass.

The single authorized saved-settings comparison reached its **45-second
watchdog** (termination/reporting completed at 45.3 s). It produced one complete
baseline phase (47 raw timing rows) and only 30 rows from the first candidate
phase. There are no second phases or completion marker. The result parser
correctly rejects this log; no speedup, stable regression percentage, completed
visual comparison or full Vulkan validation is claimed. The partial candidate
timings do not justify enabling it. No second game run was launched.

Runtime confirmed 1920x1080 DLAA, Frame Generation, Reflex 2 and the custom NR
values. The isolated manifest retains four samples/four bounces, 16x anisotropy,
full texture detail, sharpening 0.5, exposure 4 and FPS cap 85. The game process
was closed and the saved configuration SHA256 remained
`D2F899E73FA763667D22990366D7197DAE9D3483351AF439DDB03C359B77A5FE`.
Logs lack per-pipeline creation timings, so startup/driver compilation cannot
be separated conclusively from the rest of the run. The subsequent build adds
`PT_LIGHTING_PIPELINE_BEGIN/END` with CPU milliseconds around lazy pipeline
creation, outside GPU frame samples; this diagnostic was build/test checked,
not exercised by another game run. A future GPU comparison needs fresh approval.

Local evidence: `build-widescreen/rt-audit/pt-perf-map-light-cull-user-settings-20260907.log`
and `performance-map-light-cull-user-settings-20260907/settings.json` in that
directory. Reproducible offline checks: `tests/pt_map_light_cull_check.py --cxx
<g++> --sdk <VulkanSDK>`. The saved-settings scenario is `pt_map_light_cull.cfg`;
do not shorten sample acceptance or treat its incomplete log as a valid result.

### Per-hit BRDF reuse: enabled by default (2026-09-07)

`r_pathTracingBRDFReuse` now defaults to **1**. Each opaque scattering hit computes
four view/material-only terms once: normal/view cosine, squared GGX alpha,
specular sampling probability and view-side Smith masking. The five direct /
continuation evaluation sites reuse those terms; continuation sampling reuses
the same roughness term and probability. The light direction, half-vector,
Fresnel color, light-side masking, diffuse/specular split, PDF and contribution
arithmetic remain light-dependent and retain the original formulas. This does
not approximate the BRDF, change light selection, remove rays/lights, or alter
resolution, samples, bounces, textures, exposure or reconstruction quality.

The four values are invocation-local to the current hit, not a history buffer
or a per-pixel allocation. There is **no added per-resolution GPU buffer**.
The optimized shader uses a lazily created native pipeline and the existing
descriptor/push-constant layout. At the BRDF-only validation milestone, the
original shader was byte-identical to the preceding build (SHA256 prefix
`6912E92A1C09F15C`); original guide/diagnostic shaders were also unchanged.
The later map-light specialization above extends the native cache and diagnostic
selection. Pipeline failure now reports `PT_LIGHTING_UNAVAILABLE` and keeps a
safe fallback. All pipelines are released on renderer shutdown. The switch is
not archived and does not modify the user's saved graphics configuration.

Actual original/cached GLSL was executed in CPU fixtures for **484,800 BRDF
evaluations and 181,800 continuation samples per compiler configuration**:

- `-O2`: all 3,939,000 scalar results bit-identical.
- `-O3 -ffast-math`: continuation directions and three-draw ordering remain
  bit-identical; maximum relative difference is 0.00246% for BRDF output and
  0.000535% for the PDF. The test permits 0.01% relative / 1e-7 absolute numeric
  error for reassociated radiance/PDF math, but no branch/direction difference.
- Cases include grazing/back-facing views, roughness extremes, colored metals,
  black albedo, changing hits, rejected lights and sampling-probability edges.
  In an eight-visible-light fixture, view/light Smith square roots fall from
  16 to 9. That operation count is not itself a GPU performance measurement.

The single authorized saved-settings test completed normally in **31.3 seconds**
under its 45-second watchdog. Runtime confirmed 1920x1080 DLAA, Frame Generation
on, Reflex Boost and the user's custom NR values. The isolated settings manifest
also retains four samples/four bounces, 16x filtering, full texture detail,
50% sharpening, exposure 4 and the saved FPS cap. The source configuration's
SHA256 remained unchanged. No second game run was launched.

| Same-process phase | Settled frames | Median engine frame | Median GPU tracing |
| --- | ---: | ---: | ---: |
| Original A | 41 | 75 ms | 62.043 ms |
| Reuse A | 41 | 72 ms | 59.006 ms |
| Reuse B | 41 | 72 ms | 59.007 ms |
| Original B | 41 | 75 ms | 62.019 ms |

Combined tracing medians improve **4.88%**; engine-frame time improves **4%**.
The two original endpoints are stable and both optimized phases agree. These
are engine/GPU timings, **not generated/displayed FPS**, and one short static
q3dm6 run is not broad gameplay/visual validation. The optimization was enabled
after this result. The earlier emitter-search candidate remains disabled.

Build, all eight shader compilation/validation steps, 153 regression tests and
native material/environment/sky fixtures pass. Material/guide/light buffer-layout
checks pass for both new shader variants. Shader-clock profiling also selects a
matching BRDF-reuse diagnostic variant when this mode is active; that combined
diagnostic mode is compiled/validated but was not separately GPU-tested here.
This run did not enable the Vulkan validation layer or certify image quality
with a new visual comparison. Existing asset warnings remain separate.

Reproduce only with permission: `tests/run-pt-performance.ps1
-Config pt_brdf_reuse.cfg -Label <unique-label> -TimeoutSeconds 45
-VisibleWindow`. The shortened scenario warms both pipelines before its
original/reuse/reuse/original phases and leaves startup/teardown margin. Native
`PT_BRDF_MODE` messages verify actual pipeline selection. Analyze a completed
run with `tests/pt_material_program_check.py <log>`; incomplete runs are rejected.
Local evidence: `build-widescreen/rt-audit/pt-perf-brdf-reuse-user-settings-20260907.log`
and the matching `performance-brdf-reuse-user-settings-20260907/settings.json`.
Math/lifecycle tests live in `tests/pt_brdf_reuse_check.py`.

### Conservative emitter-search candidate: disabled by default (2026-09-07)

`r_pathTracingEmitterSearch` is a cheat/debug-only candidate, **default 0**.
The original full-CDF search remains the normal rendering path. The candidate
adds 64 conservative search intervals to the existing emissive-triangle table
(528 bytes including control data), then runs the same binary search inside the
selected interval. It does not replace the sampling distribution: the original
CDF, one random draw, `<=` tie handling, emitter identity, area-power PDF, MIS
weights, ray budgets and material programs are preserved.

The native index builder is linear in emitter count plus 64 bins and, when
enabled, rebuilds with the emitter list, including moving geometry/area changes.
Disabled mode skips index construction. Frozen reference scenes retain their
index, or build it on an off-to-on toggle. Bounds expand outward by one
float ULP to include multiplication rounding at bin edges; empty lists are
handled without underflow. The new fields follow the existing light/sky data;
compiled producer/consumer offset checks verify that no old fields moved.

CPU fixtures execute the actual native builder and actual GLSL sampler, comparing
the full-range and indexed modes. **3,171,351 comparisons pass in each of two
builds** (`-O2` and `-O3 -ffast-math`): identical primitive and exactly one random
draw. Cases include 1–8192 emitters, skewed/zero/rounded-away powers, exact and
adjacent bin/CDF boundaries, reordered/rebuilt lists and tiny values. This is
strong selection-equivalence evidence, not an in-game visual certification.

The one authorized saved-settings A/B/B/A run was stopped by its 45-second
watchdog (45.3 seconds including termination). It recorded four 59-row phases,
but did **not** reach its completion marker, `pt_info`, or graceful shutdown.
The standard parser correctly rejects it as an accepted performance result.
Its diagnostic-only settled medians were:

| Phase | Settled rows | Engine frame | GPU tracing |
| --- | ---: | ---: | ---: |
| Full CDF A | 53 | 75 ms | 61.991 ms |
| Indexed A | 53 | 75 ms | 61.222 ms |
| Indexed B | 53 | 74 ms | 61.589 ms |
| Full CDF B | 53 | 74 ms | 61.115 ms |

These values do not establish a consistent benefit over run-to-run/phase drift.
**No speedup is claimed; the candidate was left disabled.** The saved graphics
configuration hash remained unchanged; runtime confirmed 1080p DLAA, FG and the
user's custom NR strengths. No second game run was launched. The scenario has
since been shortened to leave more startup/teardown margin while retaining at
least 40 settled rows per phase; that revised scenario has not run on the GPU.

Evidence: `build-widescreen/rt-audit/pt-perf-emitter-search-user-settings-20260907.log`
and the matching `performance-emitter-search-user-settings-20260907/settings.json`.
Reproduce only with new authorization using `tests/run-pt-performance.ps1
-Config pt_emitter_search.cfg -Label <unique-label> -TimeoutSeconds 45
-VisibleWindow`; successful runs use the existing A/B/B/A parser in
`tests/pt_material_program_check.py`. Native/GLSL checks are in
`tests/pt_emitter_search_check.py`. Build/shader validation, 148 regression tests,
and native material/environment/sky fixtures pass. This checkpoint led to the
BRDF/view-dependent reuse work described above. Full Vulkan-layer/visual
validation remains separate.

### Sampled shader-cost diagnostics (2026-09-07)

`r_pathTracingShaderProfile 1` enables a separate, optional diagnostic integrator.
It uses `VK_KHR_shader_clock` subgroup clocks, when supported, to partition
sampled invocation time into four exclusive scopes:

- **Queries:** closest-hit/visibility ray queries and their candidate handling,
  excluding timed material/normal/emission helpers called from those scopes.
- **Materials:** hit preparation, texture/UV/color programs, normals, emission,
  sky and dielectric handling. This is not isolated texture-unit utilization.
- **Lighting:** light proposals, direct-light calculations, BRDF evaluation and
  path continuation, excluding nested timed query/material calls.
- **Other:** remaining path/pixel setup and output bookkeeping.

One whole 8x8 workgroup per 64x64 screen tile is sampled; selection rotates
across frames and handles partial edge tiles. The four counters also record
sampled paths, closest-hit queries, shadow queries and hits. Nested scopes
restore their caller's category, so their clock intervals are not double-counted.
These are **sampled, instrumented invocation-clock shares**, including scheduling
and memory stalls, not hardware-unit utilization, a precise full-frame partition,
or additive GPU milliseconds. The clock is not calibrated against Vulkan's
timestamp period. Instrumentation can perturb scheduling/register pressure;
use these results to prioritize work, then measure actual changes independently.

The normal integrator/guide SPIR-V binaries are byte-for-byte identical to the
preceding build (SHA256 prefixes `2520818689B0A663` / `E2EBE4555DE053FB`). Normal
pipelines keep their existing descriptor layouts. The extra shader, descriptor
set and coherent readback allocation are created only on diagnostic activation;
1080p uses 32,640 records / 1,044,480 bytes. Readback reuses the already-completed
render fence, with an explicit shader-write/host-read barrier and no additional
GPU wait. Unsupported capabilities or allocation/pipeline failure report
`PT_SHADER_PROFILE_UNAVAILABLE` and retain normal rendering. The switch is
cheat/debug-only, not archived, and defaults to zero.

The single authorized 45-second-bounded test exited normally in **30.6 seconds**.
It used the user's saved 1080p DLAA, FG-on, four-sample/four-bounce, 16x filtering,
exposure-4 and custom NR settings, without quality overrides. Runtime logs
confirmed DLAA/FG/NR; the source configuration's SHA256 was unchanged.

| Normal rendering phase | Settled frames | Median engine frame | Median GPU tracing |
| --- | ---: | ---: | ---: |
| Before instrumentation | 33 | 75 ms | 62.059 ms |
| After instrumentation | 33 | 75 ms | 61.571 ms |

Across 58 settled instrumented frames, clock-weighted shares were **51.01%
lighting, 28.60% materials, 18.16% queries, 2.23% other**. Sampled paths averaged
3.898 closest-hit queries, 5.429 shadow queries and 2.940 hits. Instrumented
tracing itself took a 65.845 ms median; this is diagnostic overhead, **not** a
production performance result. The before/after baseline is a stability check,
not evidence of a new optimization. These findings make repeated lighting /
BRDF / light-proposal calculations the next investigation target, ahead of
acceleration-structure rebuilding; they do not isolate which lighting subroutine
dominates. Resolution, samples, bounces and visual features remain unchanged.

Build and all six shader compilation/validation steps pass, along with 144 Python
regression tests, the native material/environment/sky fixtures and diagnostic
material-layout checks. This run did not enable the Vulkan validation layer or
perform a new visual comparison. Existing asset parsing/image-reuse warnings
remain; do not describe this as a complete Vulkan/NVIDIA validation pass.

Reproduce only with permission for a visible game test:
`tests/run-pt-performance.ps1 -Config pt_shader_profile.cfg -Label <unique-label>
-TimeoutSeconds 45 -VisibleWindow`. The runner preserves saved graphics settings.
Analyze its completed log with `tests/pt_shader_profile_check.py <log>`; incomplete,
unavailable, malformed or undersampled results are rejected. Instrumented frame
timings use `PT_PROFILE_DIAGNOSTIC`, never the normal `PT_PROFILE` prefix, so
existing performance parsers cannot silently mix the two. Local evidence:
`build-widescreen/rt-audit/pt-perf-shader-profile-user-settings-20260907.log` and
`performance-shader-profile-user-settings-20260907/settings.json` in the same
audit directory (generated test artifacts are ignored by Git).

### UV-only material input optimization (2026-09-07)

Texture-coordinate stages with supported affine/time modifiers and fixed or
wave-generated colors now skip construction of unused model/world positions,
normals and vertex/entity colors. They retain the same stage sampling program,
UV derivatives, image animation, entity shader time/scroll, blend order and
alpha coverage. Turbulence, vector/environment coordinates and entity/vertex
color generators retain full context. The immutable one-sample shortcut remains
distinct. No texture-quality, resolution, sample/bounce or reconstruction-buffer
changes were made.

`r_pathTracingMaterialFastPath` (cheat/debug, default 1) switches eligible
program markers between the optimized and original input setup for same-process
A/B tests, without resetting radiance history. Registered material slots only
are updated; sparse unused material IDs are not read. `pt_info` reports the
number of eligible stages (35 in this q3dm6 test).

The accepted performance checkpoint used the user's saved settings: RTX 5070,
1920x1080 **DLAA**, Frame Generation on, Reflex Boost, four samples/four bounces,
16x anisotropy, full texture detail, 50% DLSS sharpening, exposure 4, NR model 1
with intensity 2 / local tone 1.5 / local structure 1.7 / skin structure 1.5,
and the saved 85 FPS cap. Runtime logs confirmed native 1920x1080 tracing, FG on,
and the NR strengths. A source-file hash confirmed the saved configuration was
unchanged. The test exited normally in 39.2 seconds under its 45-second watchdog.

| Same-process phase | Settled frames | Median engine frame | Median GPU tracing |
| --- | ---: | ---: | ---: |
| Original A | 53 | 78 ms | 64.647 ms |
| Optimized A | 53 | 76 ms | 62.356 ms |
| Optimized B | 53 | 76 ms | 62.543 ms |
| Original B | 53 | 78 ms | 64.653 ms |

Combined tracing medians improved 3.41%; engine frame time improved about 2.6%.
These are **not generated/displayed FPS**. This modest improvement does not
establish smooth gameplay or performance across maps/camera motion. The earlier
DLSS Quality/FG-off test is a synthetic diagnostic, not the user's performance
baseline. The initial sandboxed baseline attempt timed out before producing a
fresh log and is excluded entirely.

Native material fixtures pass 6,720 coordinate/color/modifier combinations plus
turbulence at every modifier position; existing material, shell and sky checks,
136 Python tests, shader compilation/validation and material-layout guards pass.
No image-quality improvement or new visual-equivalence certification is claimed:
this change removes unused inputs, not noise or material features. Broader
denoising/texture-detail work remains separate.

Reproduce with `tests/run-pt-performance.ps1 -Config pt_material_program.cfg
-Label <unique-label> -TimeoutSeconds 45 -VisibleWindow` only when a visible
test is authorized. The runner now defaults to an isolated graphics-only copy
of the saved configuration, records its settings/source hash, and strips
scenario overrides of rendering quality. Use `-SettingsFile` for another saved
profile. Explicit `-Synthetic` is required for resolution/DLSS benchmark
overrides. Fresh per-label profiles and timestamp checks reject stale logs.
`-PrepareOnly` verifies the copied settings without launching the game.
Analyze completed A/B/B/A runs with `tests/pt_material_program_check.py <log>`.

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
floating-point radiance accumulation and tone mapping. It uses authored outer
skyboxes and animated cloud layers; only maps without an authored sky use the
existing low-intensity gradient fallback. Textures are
sampled without the raster gamma/intensity bake in this mode.

Map lights are read from the BSP entity data, including target-based spotlights,
instead of being reconstructed from lightmaps. Map-light selection uses a
distance/power-weighted reservoir of eight spatially proposed candidates at each
shading point. A map-built grid (at most 512 cells) stores alias tables with
80% local power/distance importance plus a 20% uniform support floor. Every map
light remains eligible, including behind doors and in reflections. The estimator
divides reservoir targets by the actual proposal PDF; it does not assume uniform
selection. No camera/PVS/visibility decision removes lights from this proposal.
Native 64x64 progressive blue-noise ranks cover pixel jitter and the first five
random dimensions; later divergent paths use independent white samples. The
table is reproducibly generated by this project, embedded in the renderer, and
needs no external texture. Neither change reduces samples or bounce counts.
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
settings changes, invalid frame gaps, map changes and camera-medium changes
reset it. Game point-light changes (including muzzle flashes) instead compare
current/prior lights at each actual shading point, with shadow visibility and
separate diffuse/specular BRDF responses. Unaffected surfaces keep history;
affected pixels shorten it according to the measured change. Persistent first
and second luminance moments support variance-aware anti-lag for other radiance
changes and guide the spatial filter. This is not replayed-path ASVGF gradients:
moving indirect illumination still relies on noisy radiance statistics and clipping.
Animated emitter CDF/index changes are
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
Temporal resets propagate to DLSS. Flat, smooth metal mirrors now use
virtual reflected-target position/depth and tracked previous target vertices.
Tracked moving mirrors use their previous plane as well as the target's previous
vertices. Native specular reprojection independently verifies both the mirror plane and
reflected object/surface; diffuse keeps its physical-hit guide. DLSS also receives
this virtual depth/motion on eligible mirrors. Roughness above 0.04, metalness
below 0.99, untracked moving mirrors, animated materials and perturbed normals keep conservative
primary guides. Native glass/procedural-water reflection and eligible glass/water
transmission now have separate histories and correspondence. Mixed dielectric pixels still
use the physical interface for DLSS motion/depth. Bounded all-transmitted chains
now support up to four tracked interfaces; general mixed optical paths and
reactive DLSS masks remain unfinished.

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
old texture/color phases, except eligible procedural-water optical transport
whose previous wave is explicitly reconstructed. Reference mode freezes material time as well as geometry.

This is **not the completed renderer or a production denoiser**. Separate
diffuse/reflection/transmission reconstruction, local direct-light reaction,
limited planar-mirror motion and bounded glass/water transmission correspondence
are implemented. Conservative per-surface rejection can still
leave noisy gameplay; general reflected motion, moving indirect light and shadows
need broader trail/flicker testing. This native pass is not NVIDIA Ray
Reconstruction, NRD or a complete SVGF/ASVGF implementation. Ordered ordinary
color overlays are now retained as described below; fog, portals and non-MD3 animation completeness remain
unfinished. Additive environment-map reflection coats on backed surfaces remain
BRDF-owned; color-bearing environment bases and unbacked effect shells are now
retained as described in the pickup repair below.
Portal/specular alpha generators and exact legacy noise-wave equivalence are
not implemented. Submitted dynamic sprites/beams can now enter the ray scene,
but complete particle/decal capture, coplanar powerup-shell composition,
reactive DLSS masks and off-camera billboard orientation need further work.
Static world, moving-object and first-person geometry use separate acceleration
structures; the static world BLAS is cached. Persistent per-object structures
and further performance optimization remain unfinished. DLSS
compatibility does not imply finished reconstruction quality. PT surface textures
now use the native filtering/mip controls and ray-footprint gradients; visibility
alpha coverage still uses the base mip, as described below.

### Missing foreground material repair (2026-09-07)

The earlier material bridge silently dropped non-additive color stages after
the first base texture. This exposed animated fire across entire polygons where
an alpha stone/metal foreground should cover it. The stock
`textures/gothic_floor/fireblocks17floor3` shader demonstrates the failure:
scrolling/turbulent lava, a stationary alpha stone overlay, then a lightmap.

The native converter now retains all eligible stages in source order, including
alpha, multiplicative, and opaque overlays. Every layer retains its own image
animation, color/alpha generators, UV transforms and sampling footprint. Color
layers are blended in texture space before conversion to physical albedo.
Additive stages remain emission, not reflected albedo; subsequent foreground
stages also mask earlier glow. Lightmaps remain excluded; the later pickup
repair distinguishes color-bearing environment stages from reflective coats.
Explicit authored base-color/light-image overrides retain
their existing policy. Geometry coverage still uses the base stage; an overlay
alpha test discards that overlay, not the underlying solid surface.

Layer count, base index, additive mask and overlay count reuse the previously
unused material tint block. This repair kept the material stride at 3,280 bytes with nine
352-byte layer slots; there are no new GPU buffers or changes to resolution,
sample count, bounces, settings, or game assets. Single-base materials retain a
fast path. Additional texture work on previously discarded overlays is required
to render those materials correctly; GPU performance has not been measured.

Offline verification for this repair:

- `tests/pt_material_layers_check.py` compiles the actual native conversion
  functions with production types and the actual GLSL composition/emission
  functions with CPU vectors and mock texture samples. Test groups cover
  the fire/stone material pattern, ordered overlays, glow masking, alpha tests,
  blend factors, separate UV programs, collapsed texture combines, all nine
  layer slots, pure-additive materials and fallback/exclusion behavior.
- All 126 existing CPU unit tests pass. All five ray-tracing compute shaders
  compile and pass SPIR-V validation; compiled material/layer offsets and strides
  and the no-large-local-record-copy guard pass. The Windows release renderer
  was rebuilt with two build jobs.
- No game/GPU test was run for this repair. These tests verify source behavior
  and binary layout, **not** in-game appearance, frame rate or a complete fix
  for the other reported visual/performance regressions.

Run the new CPU-only regression with:

```text
python tests/pt_material_layers_check.py --cc <gcc> --cxx <g++> --sdk <VulkanSDK>
```

### Authored sky restoration (2026-09-07)

The generic environment had replaced every `skyparms` sky, losing both real
six-face skyboxes and the animated cloud-only skies used by many stock maps.
The native material converter now uploads outerbox texture IDs in the parser's
`rt, bk, lf, ft, up, dn` order, cloud height and the actual cloud-stage count.
The renderer evaluates the matching face orientation from `tr_sky.c` using the
ray direction, not the UVs or distance of the BSP sky-opening polygons.

Cloud UVs use the continuous radius-4096 shell mapping from `R_InitSkyTexCoords`.
They share the surface stage evaluator for animation, scroll/scale/turbulence,
color and alpha generation. All cloud stages (including additive clouds) compose
as sky radiance in authored order. The original five-face cloud coverage is
retained; the downward cube face is not covered with fabricated clouds. An empty
sky does not accidentally display the material table's white fallback layer,
and `q3map_lightimage` remains a compiler lighting proxy, not the visible sky.
The parsed innerbox remains unused, matching this port's raster sky renderer.

Primary, reflected and transmitted rays use the sky material they actually hit.
Rays escaping the scene use a world-selected sky material independent of camera
visibility; a `skyparms` sky takes precedence over a texture-only `SURF_SKY` sky.
The selection resets on each map load. Texture-only black skies remain black.
The authored sky supplies environment radiance, so indirect/reflected lighting
can differ from the old blue gradient. The existing explicit map sun is retained;
this change does not introduce new sky importance sampling or HDR replacement art.

Sky depth is infinity with rotation-only camera motion for DLSS. The finite BSP
sky opening no longer supplies false sky parallax or depth. Animated cloud motion
itself is not yet encoded in DLSS motion/reactivity. Native sky radiance continues
to bypass surface-history reconstruction.

Metadata adds 32 bytes per material (current stride 3,312 bytes, up to 512 KiB
extra GPU material allocation) and 16 bytes in the existing light buffer. No new
per-pixel buffers, descriptors, settings, or asset packs are introduced. Sky
faces use the existing texture descriptors, mip policy and filtering controls.
The cloud mapping is continuous rather than the raster renderer's coarse
8-subdivision interpolation; exact pixel-for-pixel raster output is not claimed.

Verification: all 126 existing CPU tests pass, the actual native conversion and
material composition fixtures pass, all five compute shaders compile/validate,
and the compiled material offsets/strides and no-large-local-record-copy checks
pass. `tests/pt_sky_check.py` additionally executes the actual sky GLSL on CPU
vectors/mock texture samples: all six face orientations (96 directions), cloud
projection at four heights, cube-only/cloud-only/empty skies, ordered cloud
blends, camera-translation invariance and miss-environment selection. Source
guards cover infinite depth/rotation-only motion and map-reset wiring. The
Windows release renderer was rebuilt with two jobs. **No game/GPU test ran;
in-game appearance and performance remain unverified.**

```text
python tests/pt_sky_check.py --cxx <g++>
```

### Health and armor pickup material repair (2026-09-07)

Historical compatibility repair: its policy of retaining the health crosses'
painted base reflections is superseded by **Traced health-cross metal
reflections (2026-09-08)** above. The following describes the earlier repair;
unrelated environment artwork and armor continue using their existing rules.

The old unconditional `TCGEN_ENVIRONMENT_MAPPED` exclusion removed the health
crosses' only color textures and the additive outer shells' only effect stages.
Empty shell materials then became opaque white fallback geometry. Armor lost
its reflective backing under the colored alpha overlay. These are native
translation defects, not absent game assets.

The converter now distinguishes three cases without hardcoded pickup colors or
model-name special cases:

- A non-additive environment stage is retained as the material's authored
  color-bearing base. Red/mega health retain their separate animated electric
  layer, and armor retains its subsequent alpha color overlay.
- An environment stage in an entirely additive surface is retained as an
  independent layer, rather than an opaque fallback. The initial compatibility
  repair displayed its original reflection artwork; the thin-shell change below
  replaces that picture with traced scene reflections for pure envelope materials.
- An additive environment stage on a backed surface remains a reflective coat,
  not an area-light emitter. Water/glass/explicit dielectric reflection maps
  retain the existing physical transport policy.

The new sampler evaluates the original environment UV projection with smooth
normals and the observer of the actual ray. Existing local/world triangle data
recover the rigid model's local mapping axes, including pickup rotation and
uniform scaling. The same observer is passed explicitly through primary and
secondary shading, light-emitter sampling, coverage tests, filters, and guides;
reflections do not accidentally sample the player's camera view. Finite footprint
differences include both normal and view variation before the existing UV modifier
program, preserving mip/filter controls. Degenerate input has finite fallbacks.

This is an explicit compatibility use of the original view-dependent color/effect
artwork, **not** a claim that a 2D environment texture represents physically traced
scene radiance. Solid surfaces still use the existing GGX path-traced lighting and
reflections. Unspecified environment-based solids use the existing metal/chrome
default (roughness 0.3, metalness 0.85); explicit PBR properties win. Fully authored
PBR replacement materials remain separate work. View-dependent stages follow the
existing animated-material history-rejection policy, which can leave noisy pickup
pixels; GPU quality/performance and DLSS effect reactivity need visual testing.

Verification: the native material fixture now covers environment-only health,
rotating/scrolling/wave-driven shells, red/mega base-plus-glow, armor alpha overlays,
and authored-PBR/dielectric exclusions. The actual composition/emission fixture
also checks that observer data reaches every stage. `pt_environment_material_check.py`
executes the actual GLSL projection/basis helpers on CPU: 100 normal/view pairs,
model rotations/scales, degenerate geometry, translation invariance, distinct
reflected observers, and finite texture gradients. Source guards check observer
wiring and shell visibility. All 126 existing CPU tests, floor/sky regressions,
five compute shader compilations/validation, and compiled buffer-layout/storage
guards pass. The Windows release renderer is rebuilt. No assets, settings,
buffer layouts, per-pixel allocations or ray budgets changed. **No game/GPU test
was run; in-game appearance and performance remain unverified.**

```text
python tests/pt_environment_material_check.py --cxx <g++>
```

### Traced health-pickup shell reflections (2026-09-07)

The first pickup repair preserved the stock shells' painted environment-map
images. That restored their appearance but did not make those images reflections
of the current room. Pure environment-only additive envelopes now become
non-emitting, zero-thickness reflective/transmissive sheets in the existing
dielectric path. Mixed electric/additive effects, explicitly authored lights,
opaque health/armor materials and explicit dielectric materials retain their
respective handling. No new material-name or pickup-color table is used.

Reflected paths follow the actual scene ray; transmitted paths continue straight
through the sheet. Both sheet interfaces are accounted for with `R=2F/(1+F)`
before tint/coverage, and `T=1-R` per color channel. Fresnel selection uses
probability-compensated reflection/transmission weights, not additive brightness.
The original texture supplies only a spatially uniform tint from a fixed-coordinate
coarsest-mip sample, normalized for hue; image detail, painted highlights, UV
scroll/rotation and camera-relative projection cannot become the reflected picture.
When mip sampling is disabled, the same fixed coordinate still gives a uniform
tint, not the original reflection image. Frame and color/alpha-wave animation
can still animate tint/coverage. Explicit image-based replacement PBR art is not
required, and shell geometry no longer participates as a fake area-light emitter.

Native negative optical thickness denotes this thin-sheet mode. The radiance
path and transmission-reconstruction geometry both leave the surrounding medium
unchanged and use straight transmission without pane displacement. Shadow rays
use the same Fresnel/tint transmission instead of treating the shell as opaque.
The existing dielectric reconstruction now receives the shell; accurate temporal
correspondence on curved/moving shells remains limited by its existing eligibility
checks. Sharp reflections are used; rough dielectric-shell scattering is not
implemented. Low-sample noise and DLSS reconstruction still need visual testing.

Offline checks pass: 130 Python CPU tests, native material conversion and actual
GLSL composition/Fresnel energy/estimator fixtures, floor/sky/projection regressions,
all five compute shader compilations and SPIR-V validation, and compiled buffer
layout/storage guards. The Windows release renderer is rebuilt. No assets,
settings, buffer layouts, samples or bounce budgets changed. Optical shell hits
now consume the existing dielectric bounce budget, unlike the old additive
overlay; GPU cost is unmeasured. **No game/GPU test ran; in-game appearance and
performance remain unverified.**

### Transparent intersections and legacy glow repair (2026-09-08)

Straight additive/filter continuation previously advanced the origin by the
shadow bias and, on primary paths, reapplied the camera near plane at each layer.
That skipped geometry directly behind sprites and cut holes at intersections.
Radiance and native/RR guide rays now keep their origin fixed, advancing the ray
minimum to the next representable distance after the consumed hit. The existing
segment-distance state tracks incremental cone growth and medium attenuation;
emitter MIS and depth retain the absolute distance. No new per-pixel buffers,
sample/bounce changes, global shadow-bias reduction, or asset overrides are used.
The disabled staged prototype is regenerated from the same surface program.

Automatic health-envelope conversion now excludes vertex-deformed additive
effects. Quad/energy auras retain their authored animated glow, while the health
envelopes still trace scene reflections. Authored surface-light emission without
additive stages now uses the visible stage composition, including scrolling and
foreground alpha masks, instead of substituting `q3map_lightimage` (a map-compiler
lighting proxy). Explicit PBR/dielectric and opaque armor layer handling remain.

Validation at saved native 1920x1080 DLAA, RR on, **2 samples**, 4 bounces, FG/NR
off: `effect-intersections-before-20260908` and `effect-intersections-after-20260908`
reproduce and repair a 0.125-unit additive/wall gap and a sloping filter/wall
intersection. Both test regions had 100% missing backing normals before and 100%
correct backing normals afterward, with both checkerboard colors restored.
These are scoped regions excluding UI, weapon and the exact coplanar crossing
line, not a proof of arbitrary coincident-primitive ordering. Runs completed in
12.3/29.5 seconds with clean Vulkan validation and unchanged source settings.
`stock-effects-confirm-20260908` completed in 10.4 seconds and captured the real
q3dm6 yellow/red armor and quad weapon aura; body/weapon surfaces remain visible.
The earlier stock capture's yellow camera faced a wall and is not yellow-armor
evidence. Screens' stage/mask change is covered by actual-GLSL CPU tests, not a
dedicated in-game screen comparison. No performance improvement is claimed.

Regression checks: `pt_effects_check.py` (18 cases, including close layers,
distance/medium accounting and actual continuation wiring), native conversion
and actual GLSL tests in `pt_material_layers_check.py`, environment projection,
reflective-shell, reprojection, sampling, RR, staged-equivalence and compiled
buffer-storage/ABI checks. All 28 compute variants were rebuilt and validated,
and the release Vulkan renderer was rebuilt. The GPU image checker is:

```text
python tests/pt_effect_intersections_check.py <before-profile> <after-profile>
```

### Filtering and moving-mirror development pass (2026-09-07)

Implemented in native renderer code and embedded shaders, with no new asset pack:

- Surface texture descriptors share the raster sampler policy: `r_textureMode`,
  mip/no-mip image flags, repeat/clamp addressing, and supported anisotropy up to
  the requested `r_ext_max_anisotropy` (maximum 16). Texture resolution continues
  to follow the existing upload/`r_picmip` rules. A live `r_textureMode` change
  refreshes PT descriptors and resets both reconstruction and reference history;
  anisotropy settings retain their existing restart-applied behavior.
- Shaded hits use explicit two-axis texture gradients instead of unconditional
  mip zero. A ray cone grows with distance, is projected onto the triangle plane,
  then mapped through barycentrics into texture space. This supplies a long and
  short footprint axis for anisotropic filtering at grazing angles. Base color,
  normal, ORM and visible emission maps use the footprint. Legacy UV affine,
  scale, stretch, rotation, turbulence and vector-generation operations transform
  the derivatives as well as the texture coordinates; scroll preserves them.
- Secondary paths carry the cone width forward, with IOR scaling of angular
  spread on volume refraction. This is an approximate ray-cone footprint, **not**
  full ray differentials through curved/normal-mapped reflectors or refractors.
  Visibility/cutout acceptance and independently sampled light textures retain
  base-mip sampling; alpha-coverage-preserving mip filtering remains future work.
- Smooth-reflection spatial filtering now returns the already-preserved center
  sample without loading/refiltering neighbors whose weight would be zero.
  Rough-reflection and diffuse filter kernels, three spatial passes, render
  resolution, sample count and bounce count are unchanged. The integrator also
  skips BSDF continuation work after the final allowed interaction, while keeping
  that interaction's emission/direct illumination and its correct MIS weighting.
- Eligible tracked moving/rotating metal mirrors reconstruct the previous virtual
  target using the previous mirror plane. The temporal pass verifies that old
  plane and target rather than rejecting rotation by comparing current and old
  world-space shading normals a second time. Missing object history, perturbed
  normals and material animation still reject this special correspondence.

Offline verification: Windows NVIDIA-enabled release build succeeded, all five
compute shaders compiled/optimized and passed Vulkan 1.2 SPIR-V validation, both
integrator variants passed the large-material-storage regression check, and 51
CPU tests passed (including eight new footprint/filter/moving-plane math checks).
No game or GPU test was launched in this pass. These checks do **not** establish
a frame-rate improvement, visual quality, moving-mirror GPU correctness, or
validation-clean execution. The earlier NVIDIA issues remain open.

Next validation, only when a game run is authorized: compare oblique floors and
distant walls across bilinear/trilinear/anisotropic modes; test animated UVs,
normal/ORM maps and clamp edges; compare matched 4-sample/4-bounce performance;
and check translated/rotated tracked mirrors with moving reflected targets,
disocclusion and teleport rejection. Keep the separate native-reference,
temporal-history and raw Vulkan validation gates.

At the end of that pass, still unfinished: separate reflected/refracted motion
through glass and moving water (the next pass below adds limited native support),
general rough-reflection correspondence, moving indirect-light/shadow
stability, complete particle/decal capture, fog and portals. This pass must not
be treated as completion of those larger rendering goals.

### Independent lighting reconstruction

The integrator partitions every contribution by the **first scattering lobe**.
It evaluates both diffuse and GGX terms at the same sampled direction and
propagates their weighted throughputs independently. This is not a random
diffuse/specular label on a combined sample. A primary dielectric now assigns its
reflected branch to reflection and its transmitted branch to a separate
transmission channel. Those three terms plus directly visible emission/environment
sum to the original light-transport estimator. Subsequent interactions preserve
the first-lobe label, including later refractions and reflections.

Diffuse and reflection have separate raw radiance, double-buffered temporal history,
history count, neighborhood clipping, variance estimate and spatial scratch buffers.
Transmission has independent raw radiance, history, count and clipping, but no
spatial scratch pass or persistent variance moments yet.
Reflection rejection additionally checks view direction, mapped normal and
roughness. Mirror-like reflection/transmission never receives spatial blur;
rougher lobes permit progressively wider filter passes. Directly visible emission
is composited afterward, unfiltered. Reference accumulation is also separate for
all four channels (including emission) and never receives denoised feedback. No diffuse albedo division
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
not yet implement a rough microfacet transmission BSDF. Transmission uses its own
native history and supports the limited correspondence described below. General
mixed-reflection/refraction and normal-mapped correspondence, specialized caustic sampling
(especially point lights through refractors), scattering fog and physically
layered coatings remain outside this implementation. Static BSP water classification does not track moving
liquid brush volumes. These limitations do not turn refraction into a raster copy
or screen-space effect: off-camera geometry participates in the traced paths.

### Separate glass/water transmission reconstruction (2026-09-07)

The native integrator now separates diffuse, reflection, transmission and visible
emission all the way through accumulation and composition. Transmission is not
relabeled reflection or a raster image. Existing `r_pathTracingDebug 2` still
shows reflection **plus** transmission for compatibility; temporal debug 2 now
examines reflection history alone, and temporal debug **5** examines transmission
target/history (the same green/red/cyan/blue identity colors, gray unsupported).

Eligible flat thin panes and single water interfaces trace an additional guide
ray to an opaque target, including a tracked moving target. A bounded eight-step
inverse optical solve finds the previous camera ray through the previous
interface plane to the target's previous barycentric position. Pane displacement
and water normals share geometry functions with the radiance integrator. Water
normals are evaluated at the solved **previous position and material time**, not
at the current wave phase. This supports world-anchored procedural wave motion;
it is not a general curved/normal-mapped-interface solver.

History taps must match the old finite interface point/material/object, target
material/object, target normal, plane separation and world distance. Missing
tracking, teleports, occlusion, total internal reflection, unstable/nonconvergent
solutions and unsupported target chains reject transmission history. The native
transmission history has independent clipping and a maximum of four frames,
and receives no spatial blur. Smooth non-wavy glass reflection can independently
reuse its reflected-target history. The subsequent wavy-reflection pass below
extends target motion to eligible procedural water reflections as well.

At the end of that pass, nested glass, multiple optical boundaries before the
opaque target, normal-mapped interfaces, layered effects and animated target
materials did not get this motion reuse. The bounded nested-transmission pass
below extends the first two cases. The DLSS interface remains separate:
one motion/depth pair cannot represent both dielectric lobes, so mixed pixels
retain physical-interface motion/depth. No reactive DLSS mask is implemented here.

Lifecycle additions: six descriptor bindings and six buffers (including the two
history buffers), with guide/history swaps, capability/range checks and explicit
teardown. The full-resolution allocation cost is **160 bytes per scene pixel**,
about 141 MiB at 1280x720 or 316 MiB at 1920x1080. This is not claimed as a
performance optimization. Ordinary opaque pixels skip correspondence reads/writes;
the extra optical guide rays/solve are skipped when native temporal reconstruction
is off. Render resolution, radiance sample count and bounce count are unchanged.

Offline verification: release renderer rebuilt; all five compute shaders passed
Vulkan 1.2 SPIR-V validation; both integrator variants passed the storage-copy
regression; 63 CPU tests passed, including twelve new inversion/transport/binding
checks. Tests cover pane thickness, IOR 1, moving targets, transformed interfaces,
previous wave time, underwater orientation and total internal reflection.
**No game or GPU tests were run.** Visual quality, actual history reuse, GPU cost,
restart/resource validation and DLSS behavior remain unverified for this pass.
Existing NVIDIA validation/restart issues are not closed by these offline checks.

### Wavy-water reflection reconstruction (2026-09-07)

Native reflection history now has a separate correspondence path for procedural
water. An additional deterministic guide ray identifies the reflected opaque
target, including tracked moving targets. The inverse optical solver finds the
previous camera ray using the previous interface plane, the target's previous
barycentric position, and the wave at the solved previous point and material time.
It uses the same wave function and geometric-hemisphere/grazing fallback as the
radiance path. Above-water and underwater orientations are handled explicitly.
This is not a planar-mirror approximation or a screen-space reflection.

The temporal pass projects the solved **interface point**, not the real target
(which may be behind the camera). It validates both the finite old water surface
and the real old target, including material/object identities, normals, plane
separation and distance. Target footprint uses the camera-to-interface-to-target
path length. Reflection history is independently clipped, reacts to the existing
local-light-change probe, and is capped at four frames. Nonconvergent or missing
correspondence rejects history; unsupported water targets explicitly reject
surface-motion fallback instead of reusing an unrelated reflected image. Planar
virtual-target guides and real water-target guides cannot reuse each other's taps.
Temporal debug **3** now covers both supported reflected-target types.

**No additional per-pixel buffers or descriptor bindings.** Water reuses the
existing 32-byte reflection-motion record for the previous target, validity,
previous interface point, and a 32-bit octahedrally encoded target normal. Identity
is supplied by the current guide for the same tracked triangle. Planar motion
keeps its existing layout. The previous transmission pass's 160-byte/pixel
addition (about 316 MiB at native 1080p) remains; this pass does not remove that
cost. Total working buffers remain 624 bytes per render-resolution pixel.

Only flat geometric interfaces with the built-in procedural wave and opaque,
non-animated target materials qualify. Curved/authored-normal-mapped interfaces,
multiple reflective/refractive boundaries before a target, moving indirect-light
correspondence, and general particle/decal/fog/portal completion remain unfinished.
At the end of that pass, radiance traced supported nested media but nested
reconstruction was still absent; the next pass adds bounded transmission chains.
DLSS still receives the physical interface motion/depth on mixed
dielectric pixels, not these separate native reflection/transmission motions.
The extra water guide ray and bounded solve run only with native temporal enabled;
no radiance samples, bounces, resolution or existing filters are reduced.

Offline verification: the Windows NVIDIA-enabled release renderer rebuilt; all
five compute shaders compiled/optimized and passed Vulkan 1.2 SPIR-V validation;
both integrator variants passed the large-material-storage guard; and 75 CPU tests
passed. Twelve new tests cover reflected-path inversion, previous wave time,
moving targets and transformed interfaces, underwater/grazing views, targets
behind the camera, 32-byte motion storage/normal precision, and rejection rules.
These are CPU math/source-contract tests, not execution of the shaders on a GPU.
**No game or GPU tests were run.** Actual history reuse, visual quality, frame time
and NVIDIA validation/restart issues require later runtime verification; no speed
improvement or validation-clean GPU execution is claimed.

### Bounded nested-transmission reconstruction (2026-09-07)

Transmission guides now follow an **all-transmitted chain of up to four optical
interfaces** before an opaque, non-animated target. This includes multiple thin
panes, entry/exit faces of closed glass, and nested glass/water combinations with
tracked moving targets and intermediate interfaces. A pane counts as one guide
interface because the existing parallel-pane transport already includes its
equivalent exit displacement. Closed volume entry and exit count separately.
The guide budget also respects the configured radiance bounce count: four
interfaces plus the target require at least five bounces. Settings are never
raised or reduced automatically.

The guide traversal uses the same medium-stack rules as radiance transport:
camera-medium initialization, inferred initial back faces, nested IOR push/pop,
and panes that leave the surrounding medium unchanged. Each eligible interface
records its previous tracked plane, optical ratio/thickness and wave orientation.
The bounded inverse solve replays **all** those interfaces at the previous
material time. The one-interface case retains the existing solver. Iterations
use plane/Snell/pane math, not additional ray queries. After convergence, nested
paths must remain inside the previous tracked triangles of every interface.
Leaving an old pane, crossing surfaces in the wrong order, missing tracking,
total internal reflection, exceeding either bound or failing to converge rejects
history rather than using a truncated/single-interface path.

Each current/previous target guide now carries an ordered two-word rejection
fingerprint plus an explicit interface count. The fingerprint includes interface
material/object IDs, optical parameters, absorption, side/wave flags and
intermediate triangle IDs. Temporal taps and clipping neighborhoods must match
that sequence and the target identity, in addition to the existing finite first
interface and real target geometry/normal/visibility tests. Rebatching intermediate
geometry or crossing a triangulation edge can conservatively reject otherwise
usable history. The 64-bit fingerprint is not a collision-proof identity or a
substitute for the geometric tests.

**No added per-pixel buffers or descriptor bindings.** The existing 32-byte
transmission target records pack the target normal into one word, leaving four
integer words for target identity, the fingerprint and count. Count zero is the
validity flag; packed floating-point normal bits are never tested as a number.
The 48-byte motion record and all allocation sizes remain unchanged. Total working
buffers remain 624 bytes per render-resolution pixel, including the earlier
160-byte/pixel transmission addition (about 316 MiB at native 1080p). Additional
traversal/solver work is bounded and limited to eligible optical guide paths with
native temporal reconstruction enabled; this is not a measured speed improvement.

This reconstructs the chain's directly transmitted opaque target, **not every
mixed reflected/refracted branch contributing to transmission radiance**. Paths
longer than four interfaces, curved/authored-normal-mapped interfaces, intervening
layered effects, animated targets, arbitrary internal-reflection correspondence,
moving indirect-light/shadow reconstruction and broader particle/decal/fog/portal
completion remain unfinished. Transmission retains its independent clipping and
four-frame history cap. DLSS still receives physical-interface motion/depth for
mixed dielectric pixels, without a new reactive mask or NVIDIA reconstruction.

Offline verification: Windows NVIDIA-enabled release renderer rebuilt; all five
compute shaders compiled/optimized and passed Vulkan 1.2 SPIR-V validation; both
integrator variants passed the material-storage guard. Compiled guide, temporal
and filter shaders also passed explicit 32-byte transmission-record stride,
member-offset and integer-metadata checks. All **94 CPU tests passed**, including
19 new nested-medium/inversion/rejection/packing contracts.
**No game or GPU tests were run.** Actual history reuse, visual quality, GPU cost,
resource/restart behavior and the existing NVIDIA validation issues remain
unverified by this offline pass.

### Internal-reflection paths, native marks and static uploads (2026-09-07)

Transmission-guide replay now handles **total internal reflection after the
first transmitted interface**, including transmission/reflection/transmission
chains. Each event records its reflection/transmission branch in the existing
ordered fingerprint. Replay checks the critical angle again at the previous
time; a branch change rejects history. Internal reflection leaves the nested
medium stack unchanged. The four-event/configured-bounce bounds, finite previous
triangle checks and four-frame history cap remain. A first-event reflection
still belongs to the reflection signal. This does not reconstruct arbitrary
Fresnel-selected mixed branches or curved/normal-mapped optical interfaces.

Submitted polygon effects now enter the native ray scene separately from cached
BSP geometry, even when they share a shader/entity sort key. Tessellation
overflow preserves that classification. Polygon normals use the complete fan
boundary rather than assuming its first three vertices are non-collinear.

Non-emitting opaque, alpha and filter polygon-offset marks are attached shading
layers on static receivers, not displaced occluders or a raster overlay. Primary
and secondary shaded hits query the dynamic BLAS for coplanar marks and apply
their color with receiver-derived texture gradients. Fan edges are deduplicated;
layers are sorted by captured primitive order rather than BVH traversal order.
At most 16 overlapping layers are retained at a hit. Complementary filtering
(`ZERO, ONE_MINUS_SRC_COLOR`, used by fading legacy marks) is supported in both
material shading and visibility. Marks do not cast floating shadows or enter the
emitter distribution. Receiver color changes conservatively reject native
history, including reflected/transmitted targets and the first frame after a
mark disappears. This can be noisy; it is not temporal reconstruction of marks.
Moving receivers, emitting/normal-map decals, broader particles, fog and portals
remain unfinished. No decal geometry offset or PK3 is introduced.

Static world positions, indices, vertex attributes and triangle material IDs
now remain in their CPU/GPU buffers after the first upload. Map replacement and
renderer recreation invalidate the cache; moving objects, weapon geometry and
marks still upload every frame. Animated material records retain their separate
dirty/update path. Transfer ranges are capacity checked. `pt_info` reports the
last recorded geometry/instance upload byte count. This removes redundant data
transfers without changing samples, bounces or resolution, but its frame-time
benefit has **not been measured**. These changes add no per-pixel buffers; decal
scene bounds add 32 bytes per scene. The previously documented 624-byte/pixel
working-buffer budget is unchanged.

Offline verification: the NVIDIA-enabled Windows release rebuilt; all five
compute shaders compiled, optimized and passed SPIR-V validation. Both
integrator variants and the temporal/filter transmission layouts passed their
compiled storage checks. All **126 CPU tests passed**, including mixed-path
replay, native mark math/capture, persistent-prefix updates and screenshot timing.
These are CPU math/byte models and source contracts, not GPU correctness proof.

#### Bounded GPU checkpoint: failed shutdown, not a visual pass

The single approved windowed checkpoint used isolated settings at 640x360,
native 4-sample/4-bounce path tracing, with DLSS/FG/NR settings all zero. The
render script reached `PT_SHORT_COMPLETE`, but the process stalled inside
Streamline's NGX teardown. The 40-second external watchdog terminated its own
child after **41.1 seconds (exit 124)**, within the user's 45-second allowance.
The game was confirmed closed. No second game/GPU run was made.

`pt-effects-mixed-20260907-01.log` under `build-widescreen/rt-audit` records attached
decal triangles increasing from 6 to 12. This proves submission, not appearance.
The queued after-capture picked up the subsequent temporal debug view; the final
capture was not flushed before quitting. The config now waits three frames
after each screenshot before changing state or quitting, but that correction
has not been rerun. This map reported no dielectrics, so the checkpoint did not
exercise the new optical-path reconstruction. The static-prefix optimization
was implemented after this run and is also not GPU-validated.

The raw validation log contains the already unresolved NVIDIA pacer image-layout
VUID `09600` and presentation-semaphore VUID `00067`, naming
`nv.sl.dlss_g` objects even with FG disabled. It has no completed instance-end
marker because teardown stalled. Local SDK/source inspection places the last
message at the NGX shutdown call, but does not establish the blocking thread or
root cause. No vendor DLL was modified, cleanup bypassed or error suppressed.
Another explicitly authorized, bounded diagnostic run is needed to capture the
shutdown stacks before attempting a causal fix. The renderer remains WIP and
this checkpoint is **not** a clean-exit, visual-quality or performance pass.

The diagnostic launcher now uses an independent owned-child watchdog, rather
than collecting stacks before enforcing its timeout. Hang capture starts with
five seconds left and checks the deadline while walking threads/modules/frames.
An offline C fixture verified deadline termination despite a delayed calling
thread, cancellation and an already-expired deadline. A real debugger launch of
that console fixture also exited 124 at its two-second watchdog (2.46 seconds
including launcher cleanup), with no fixture child left running. These checks
launched no game and used no GPU. They do not diagnose the NVIDIA hang itself.

### Development controls

| Setting | Default | Meaning |
| --- | ---: | --- |
| `r_rayTracing` | 0 | 2 selects this experimental mode after `vid_restart` |
| `r_pathTracingSamples` | 2 | Fixed samples per pixel per rendered frame, 1-64; a ceiling if adaptive sampling is enabled |
| `r_pathTracingAdaptive` | 0 | Optional RR adaptive sampling; off keeps the requested fixed sample count |
| `r_pathTracingBounces` | 4 | Maximum surface interactions, 1-12; 1 is direct-only |
| `r_pathTracingExposure` | 1 | Live, saved linear exposure before tone mapping, 0.01-16. Graphics menu: RTX Exposure, 0.0625x-16x in quarter stops; Reset restores 1x. |
| `r_pathTracingAmbient` | 0 | Live, saved console-only diffuse ambient fill, 0-2. Zero disables; try 0.05-0.15. Separate from exposure; fills surfaces and fog without additional rays. |
| `r_pathTracingMaterialFastPath` | 1 | Cheat/debug A/B switch for UV-only material input elimination; no quality-setting changes |
| `r_pathTracingReference` | 0 | Cheat-protected frozen geometry/light snapshot for convergence testing; not a gameplay mode |
| `r_pathTracingDenoise` | 1 | Native reconstruction master switch; bypassed in reference mode |
| `r_pathTracingTemporal` | 1 | Camera/object-reprojected radiance history before spatial filtering |
| `r_pathTracingHistory` | 8 | Maximum temporal frame count, 1-32; moving geometry/roughness may shorten it |
| `r_pathTracingTemporalDebug` | 0 | 1 diffuse history, 2 reflection history, 3 reflected-target history (planar mirrors and procedural water), 5 transmission-target history: green reused world, red rejected/new world, cyan reused objects, blue rejected/new objects, gray unsupported. 4 shows local light reaction (red diffuse, blue specular, green unaffected) |
| `r_pathTracingSampling` | 1 | Cheat diagnostic: 1 native early-dimension blue noise; 0 white noise. Spatial light proposals stay enabled; changing this resets history |
| `r_pathTracingDebug` | 0 | Cheat-protected: 0 beauty, 1 diffuse, 2 reflection/transmission, 3 visible emission, 4 mapped normals, 5 roughness/metalness, 6 dielectric classification |
| `r_pathTracingTestScene` | 0 | Cheat-protected, renderer restart: 1 material fixture, 2 mirror/moving target, 3 transparent intersections at X=10000; no PK3 |
| `r_pathTracingTestMotion` | 0 | Mirror fixture only: 0 stationary target, 1 moving, 2 removed |
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
624 bytes per render-resolution pixel (about 4.82 GiB at native 3840x2160), before
scene storage and DLSS images. Mirror guides, temporal moments and local-light
probes add 144 bytes/pixel over the previous split-history renderer; separate
transmission adds a further 160 bytes/pixel. DLSS uses
its lower internal render dimensions. Allocation is recreated on renderer/resolution
restart. Large CPU scene/pose caches use renderer-owned allocations, not the
engine's small zone heap. They are freed on shutdown/map replacement as appropriate.

The renderer export ABI is now version 10; rebuild the executable and all renderer
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

### Reforged-informed reconstruction and sampling

The architecture comparison used [Quake-III-Arena-R's reforged branch](https://github.com/fknfilewalker/Quake-III-Arena-R/tree/f85ead64d13be64196aa665325e086337bd16caf),
especially its ASVGF temporal/gradient passes, clustered light selection and
virtual mirror motion. This implementation is new native VQ3 Evolution code;
it does not copy that project's shaders/assets, its older NV ray-tracing backend,
or claim to port its full ASVGF pipeline. Our modern KHR ray queries, material
transport, full-support light selection and separate lighting channels remain.

Guide generation is a separate compiled compute pass (`pt_guides.comp`), sharing
material/traversal helpers with `pathtrace.comp` through `pt_integrator.glsl`.
Each compiler entry point removes unused functions; the radiance kernel no
longer carries guide/reprojection query state. A compute write/read barrier
separates the passes. Temporal moments and reflected guides are double-buffered
with the existing completed-frame fence and validated history lifecycle.

Regression additions:

- `pt_sampling_check.py`: exact nonuniform RIS expectations (including duplicates
  and occluded lights), support-floor and alias-probability checks.
- `pt_sampling_quality_check.py`: native table reproducibility/permutation and
  progressive-threshold spectra (requires NumPy); low-frequency power is 1.8–5.5%
  of white noise in the tested 10–90% masks. This is not a scene-quality claim.
- `pt_mirror.cfg` / `pt_mirror_check.py`: static mirror with an off-camera tracked
  target, camera turn, removal and recovery. Native diagnostic geometry, no PK3.
- `pt_local_lighting.cfg` / `pt_local_lighting_check.py`: actual stock-map muzzle
  flashes, local reaction debug masks and no global history-reset counter changes.
- Existing temporal, material/optics, transition and validation checks still apply.

`tests/run-pt-regression.ps1 -Config pt_mirror.cfg -Label mirror` runs an isolated
hidden-window native test (optional `-Dlss 1`, `-Width`, `-Height`). It refuses to
run beside an open game, bounds the owned process with a watchdog, and copies
logs under `build-widescreen/rt-audit`; the tests require the installed game data.
Run the matching Python image checker only after successful completion. Validation
settings/SDK paths in the runner target this Windows development installation.

### Rebuilding embedded ray shaders

The checked-in `Compiled/*_comp.c` arrays are linked by the normal build. To change
ray shaders, run `code/renderer_vulkan/shaders/compile-raytracing.ps1` with
`-VulkanSDK <installed SDK directory> -CC <gcc executable>`, then rebuild the game.
The script compiles for Vulkan 1.2, runs `spirv-opt -O`, validates SPIR-V and regenerates the embedded
C arrays. Add `-Raster` to also regenerate/validate all six raster vertex/fragment
shaders. The Linux shader script also covers compute shaders. No PK3 is produced.

Reconstruction/sampling regression results (2026-09-07): all five embedded
compute shaders passed SPIR-V validation, and both integration/guide binaries
passed the storage-copy check. Twelve CPU sampling/spectrum/projection checks
passed. The native mirror fixture reused 99.7% of moving-target history and
96.9% during a camera turn; the DLSS Quality run exceeded 99% in both cases.
Removal/recovery checks passed. Stock-map muzzle flashes produced 12 local
updates with no global reset, while unaffected surfaces remained unchanged.
Temporal reconstruction reduced display-space MSE 28.0% and four-frame variance
90.4% against spatial-only filtering in the one-sample fixed-view test. This
comparison checks temporal filtering, not improvement over the preceding build.
Native material checks passed (1.216/255 channel-recomposition MAE), as did stock
water/glass and DLSS restart/ultrawide/map/menu transitions.

Final camera-verified RTX 5070 captures, 1080p output, NR enabled, FG off,
four samples/four bounces, 599 profiled frames each:

| Mode | Median frame | 95th percentile | Worst frame | Median tracing + guides |
| --- | ---: | ---: | ---: | ---: |
| DLSS Quality (1280x720 tracing) | 23 ms | 24 ms | 25 ms | 16.128 ms |
| DLAA (1920x1080 tracing) | 48 ms | 53 ms | 55 ms | 36.021 ms |

The benchmark now allows longer unprofiled connection/teleport settling and logs
the actual camera (both final runs: -1504, 200, 50, yaw 0; Quake resolves the
requested point out of the adjacent wall). Earlier logs did not record this, so
their 50/24 ms figures are historical context, not a strict controlled speedup
claim. An earlier fast-startup Quality run reported 21 ms but was superseded by
this verified capture. Defaults and saved user settings were not reduced or changed.

One material-test `vid_restart` terminated with Windows exception `0xc0000409`
inside `sl.interposer.dll` 2.12.0 at offset `0x6d249`, during logical-device
creation, before the new path-tracing resources were initialized. The immediate
retry and later repeated-restart test passed. This is an unresolved intermittent
Streamline restart failure, not a proven root cause or a fixed issue. Logs and
the Windows event were retained; the stalled follow-on test's crash marker was
preserved in the isolated profile. Successful runs still reported the known
NVIDIA-internal 09600/00067 diagnostics, with no unexpected validation messages.

### Renderer lifetime hardening (2026-09-07)

Native source fixes now release frame resource tags before destroying their
images, destroy swapchain framebuffers before their shared depth attachment,
and check Streamline shutdown results before destroying the Vulkan device.
The separately initialized Neural Rendering snippet now has a matching
device-specific shutdown through the existing native bridge. Its parameters
are destroyed while the Streamline-owned NGX core is still initialized, rather
than after core/device teardown. Repeated cleanup and partial attachment
failures use the same idempotent shutdown path. No vendor DLL was patched or
pinned in memory, and no rendering quality setting was reduced.

`tests/pt_restart.cfg` recreates the renderer/device 12 times in a loaded q3dm6
game. `tests/pt_restart_check.py --lifecycle` requires all 12 restarts to resume
path tracing and all 13 final shutdowns to release resources in order. Optional
`--neural-rendering` and `--frame-generation` checks require actual evaluation
and generated-frame markers in every renderer lifetime, not just enabled cvars.

The preceding lifetime build completed four 12-restart runs on RTX 5070: native 640x360;
DLSS Quality + NR at 640x360; and DLSS Quality + NR with Frame Generation
requested at 960x540 and 1920x1080. The NR runs recorded successful evaluation
after every restart. The Frame Generation-requested runs did **not** record
generated-frame activation, so they verify lifecycle completion, not working
frame generation. A separate 1080p restart/ultrawide/map/menu transition run
also completed. Validation still reports the known NVIDIA pacer layout and
semaphore issues. Checking the full console across every lifetime found no
additional validation signatures in these successful runs.

The unchanged 1080p Quality + NR benchmark recorded 599 camera-verified frames:
23 ms median, 25 ms p95, 50 ms maximum; trace + guides median 16.110 ms.
The preceding capture was 23/24/25 ms. Median performance is unchanged, but the
new capture contains a larger outlier and is not evidence of improved frame
pacing. Samples, bounce counts and saved user settings are unchanged.

Restart reliability is **not resolved**: an instrumented Frame
Generation-requested run still hit an execute access violation during
`vkCreateDevice`. The captured call chain goes through `sl.common` / NGX core /
`nvngx_dlssg!NVSDK_NGX_VULKAN_Init_Ext2` to an address outside the currently
loaded modules. It is consistent with a stale logging callback into a previous
`sl.common` lifetime, but the callback ownership/root fix has not been proven.
See `pt-restart-lifetime-fg-stack.log` in the ignored audit output. Successful
retries do not supersede this failure; the prior `0xc0000409` report also remains
open. Do not describe this build as restart-stable or validation-clean.

Diagnostic additions:

- `tools/pt-crash-capture.c` builds a Windows-only launcher that debugs only its
  own child, records first-chance access violations before engine signal cleanup
  obscures them, and captures thread stacks before its watchdog stops a hung
  child. It preserves recently unloaded module ranges for stale-callback
  diagnosis. It writes local text, not dumps or uploads, and changes no registry
  settings. Symbols are loaded lazily; the child uses normal-launch heap
  behavior instead of Windows' slow debugger-only heap checking.
- `tests/run-pt-regression.ps1` accepts NR/FG settings and `-DebugCapture` and
  preserves failed-run logs. Its isolated profile does not edit saved settings.
- `tests/pt_validation_check.py --console` checks validation callbacks from all
  renderer lifetimes. The layer's separate output file can be reopened on
  instance recreation and must not be the only source for a restart audit.
  Four parser tests verify known/unknown errors and exact duplicate notices.

### Surface routing and presentation audit (2026-09-07)

Further native integration changes:

- Create/destroy Win32 surfaces through Streamline's explicit exported hooks.
  Its generic instance-function lookup bypasses these hooks. The previous
  `Could not find a window corresponding to this application` error is absent
  after the correction. Destroy the routed surface while the plugins are live.
- Stop asynchronous Frame Generation before map-resource teardown. Explicitly
  free used SR/FG resources before releasing frame images, and drop our NR
  module references before unloading Streamline's NGX/logging providers.
  Return codes and the complete teardown order are logged.
- Implement restart-applied `r_swapInterval` in Vulkan: immediate presentation
  for `0` when supported, FIFO for `1`. Supported FG requests use immediate mode
  with an explanatory message; Vulkan FG does not support VSync. Lack of an
  immediate surface mode is reflected in feature availability, not by rewriting
  the requested setting. Both selections were exercised in isolated tests.
- Move Reflex pacing before CPU scene construction, and include command
  recording in its render interval. Obtain one SDK-generated frame token per
  rendered frame. `nvidia_info` / `pt_info` expose cached presentation/status and
  reset counters and window focus without consuming another SDK statistics query.
- Build the SDK-free boundary stubs when `USE_NVIDIA_DLSS=0`. Both enabled and
  disabled Windows builds compile; this does not certify other platforms.

The controlled `pt_frame_generation.cfg` test runs static and turning views at
1080p Quality, NR model 1, four samples/four bounces. It repeatedly reports
114/114 and 234/234 query/presentation counts, peak 1, success status, and just
one initial history reset. The new `pt_frame_generation_check.py` **fails**
these captures. A final capture recorded **focused 0, minimized 0** at both
checkpoints. This is an unfocused-window result, not evidence of failure during
focused gameplay. Foreground activation and display pacing still need testing.
Seven synthetic tests require sustained extra presentations in both phases,
reject partial/SDK-error results, and mark unfocused checkpoints inconclusive.

Three 12-restart runs completed with live path tracing and NR after every restart.
The second run produced 2x presentation counts in seven renderer lifetimes, then
returned to 1x in the remaining checkpoints. The final raw-audit restart run
showed the same pattern. Focus was not recorded across those earlier lifetimes,
so a focus transition has not been excluded. This is not a working-FG
certification. Restart/resize/map/menu transitions completed.
The unchanged 599-frame, camera-verified Quality + NR benchmark measured
23/25/50 ms median/p95/maximum; trace + guides median was 16.222 ms. This is not
evidence of improved performance or elimination of stutter.

**Validation correction:** the previous console-only checks were incomplete.
Streamline's messenger disappears before `vkDestroyDevice`, and some of its
synchronization messages omit their diagnostic/object headers. The separate
layer file only retains the last instance. Reinspection found
`VUID-vkDestroyDevice-device-05137` in older retained files too. A complete raw
capture identifies 67 `nv.ngx.dlssg.resource` buffers and 67 memory allocations
left alive after using Frame Generation. The issue persists after explicit
`slFreeResources` returns success. Native and SR-only controls did not show
that leak; NR-disabled FG captures also contain it. A map-transition capture
reported 268 leaked objects. Do not manually destroy these private SDK handles.

`run-pt-regression.ps1` now gives each label a unique `*-raw-validation.log`.
The opt-in engine-owned messenger writes full messages directly to that file,
appends all instance lifetimes, and survives through SDK/device teardown. It
does not route asynchronous messages through the engine console. The runner
restores its environment afterward. Check the raw file with
`pt_validation_check.py --instances 13` for a 12-restart run; keep the console
and layer file as supplementary evidence. Six parser tests ensure incomplete
vendor messages and teardown leaks remain failures. The leak has **not** been
added to the accepted-diagnostic list.

The final raw audit retained all 13 instance lifetimes and failed its gate:
40 recognized unresolved messages, 28 synchronization messages lacking the
command-buffer naming needed by the strict classifier, and six device-leak
messages. These were not suppressed or relabeled as passes.

Remaining work includes focused-gameplay FG verification, private FG resource leaks,
pacer layout/semaphore and FG clear/copy synchronization diagnostics, and the
previous intermittent NGX restart crash. Successful restarts and shutdown API
results do not close those issues. No vendor DLL was modified or pinned, and
the user's saved profile and rendering-quality settings are unchanged.

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
GPU timestamps for raster, scene upload/build, guide generation/attribute upload, integration,
temporal filtering, spatial filtering and post-processing. It is off by default;
it reads queries only after the existing fence, never adding a query wait.
`tests/run-pt-performance.ps1` runs `tests/pt_performance.cfg` in a fresh isolated
`build-widescreen/rt-audit/performance-<label>` profile, with validation disabled
for timing. It now copies saved graphics settings by default; historical fixed
resolution/DLSS overrides require `-Synthetic`. Use
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
