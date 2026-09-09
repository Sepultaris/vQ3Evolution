# Ray Reconstruction development record: 2026-09-08 through 2026-09-09

This is an archived implementation and measurement record, not the current
setup guide. Results are scoped to their dated build, scene, settings and exit
status; do not combine different runs into a general FPS promise. The newest
workgroup experiment did not establish a speedup, and the shutdown timeout is
unresolved. See the [current RR guide](../RAY_RECONSTRUCTION.md) and
[current status](../STATUS.md). Source/test paths in backticks are relative to
the repository root; raw local audit artifacts are intentionally not in Git.

Implemented through NVIDIA Streamline 2.12.0's public DLSS-RR interface, in the
Vulkan renderer and native graphics menu. It uses the official SDK's production
`sl.dlss_d.dll` and `nvngx_dlssd.dll`, copied by `USE_NVIDIA_DLSS=1`. No PK3,
sample application ID, Neural Rendering DLL or replacement game assets are
needed. Build output: `build-widescreen/release-mingw64-x86_64/vQ3Evolution.exe`.

## Enable at native resolution

In Graphics Options, select Path Tracing, NVIDIA DLSS **DLAA**, and **DLSS Ray
Reconstruction: On**, then Apply. Console equivalents:

```text
set r_rayTracing 2
set r_dlss 5
set r_dlssRayReconstruction 1
vid_restart
```

`pt_info` reports support, enabled state and successfully evaluated RR frames.
The activation log must say `1920x1080 -> 1920x1080` for 1080p DLAA, not the
1280x720 input of Quality mode. Frame Generation was off in all measurements.
NR is bypassed while RR runs, without overwriting its saved preference.

RR now defaults **on** on supported hardware with path tracing and a DLSS/DLAA
mode selected. An explicitly saved `0` remains an opt-out. It does not change the
selected DLSS mode or bounce count. Sampling defaults to a flat **2 samples per
pixel**, with `r_pathTracingSamples 2` and `r_pathTracingAdaptive 0`. Explicitly
enabling adaptive sampling makes the requested count a ceiling and can halve
it on eligible stable surfaces (including 2 to 1). Existing saved choices remain.
Test profiles never rewrite the user's saved configuration.

## Pipeline and input contract

1. A separate guide pass exports physical depth/motion, linear diffuse albedo,
   directional-hemispherical specular albedo, world-space unit shading normals,
   linear roughness, and primary-surface-to-reflection-hit distance.
2. An RR-specific combined-radiance transport pipeline traces the selected
   budget and unchanged bounces/materials. Optional map-light reservoirs change
   primary direct-light selection; adaptive sampling changes per-pixel counts.
   The native/reference transport and filters remain available separately.
3. The normal RR tracer writes fresh RGBA16F noisy HDR radiance directly. Native
   diagnostic/staged transport retains its separate outputs and packing pass.
   No native temporal history, exposure, gamma or tone mapping enters this buffer.
4. NVIDIA RR reconstructs HDR output. Native temporal/spatial filters, NR and
   the normal DLSS color evaluation are bypassed for this frame.
5. The existing exposure/tone-curve and contrast-adaptive sharpening math runs
   on RR's output, then the HUD/menu is drawn at output resolution.

Reflection distances use secondary-ray visibility, without camera near-clip or
first-person weapon priority. Camera matrices, jitter removal and motion units
match the Streamline input contract. Camera cuts, renderer restarts and native
diagnostic/RR transitions invalidate history. The four reflectance/geometry
guides plus HDR input/output and display image add approximately **95 MiB at
native 1080p**, excluding NVIDIA's internal allocations. Existing native buffers
are retained for fallback.

Missing SDK/GPU support keeps native reconstruction. RR allocation failures
unwind partial resources. An evaluation failure disables RR for the session;
the failed frame explicitly tone-maps fresh HDR for DLSS fallback, and subsequent
frames resume native filtering with reset history. This can produce one noisy
transition frame. Tagged resources are released after draining GPU work and
before image destruction.

## Measured DLAA results

### RR workgroup experiment (2026-09-09)

The combined-radiance tracer was tested with 8x128, 8x64 and 8x32 workgroups.
Only dispatch grouping and specialization constants differ: pixel coverage,
per-pixel seeds, shader arithmetic, samples and bounces are unchanged. The
existing automatic policy is retained (8x128 on the tested RTX 5070); neither
smaller candidate established an improvement.

`r_pathTracingRRRows` is a non-archived, cheat-protected, latched diagnostic
override: `0` selects the existing automatic policy; `8`, `32`, `64` and `128`
request rows in an eight-column group. Apply a test override with `vid_restart`.
Unsupported requests fall back to the hardware-limited automatic choice. A
device unable to run even 8x8 cannot create this RR pipeline and follows the
existing resource-failure fallback. `PT_RR_WORKGROUP` records the actual choice.

Matched captures `rr-groups-128-b-20260909`, `rr-groups-64-a-20260909` and
`rr-groups-32-a-20260909` used 1920x1080 DLAA, RR, flat two samples, adaptive
sampling off, x16 filtering, exposure 5, sharpness 0.7 and FG/NR off. The user's
saved bounce count had changed to **six**, which was preserved; do not compare
these timings directly with the earlier two-bounce results below. Each run
contains 31 profile records (25 after trimming three at each edge), with the
same camera at (-1361, 200, 77), yaw 0, at both measurement endpoints.

| Workgroup | Median trace | Median engine frame |
| --- | ---: | ---: |
| 8x128, existing default | 23.948 ms | 32 ms |
| 8x64 | 27.842 ms | 36 ms |
| 8x32 | 40.962 ms | 49 ms |

These are **diagnostic rendering-phase observations**, not accepted end-to-end
performance passes. All three finished their scenario and screenshot, but the
45-second guard terminated them while `slShutdown` was pending. Shutdown began
at approximately 40.8, 42.0 and 43.5 seconds respectively; no return marker was
observed. This unresolved lifetime issue also affects the existing default.
The source/shared profiles and renderer/executable hashes matched. The 64/32-row
captures differ from 128 by less than 0.103 mean channel levels out of 255 in
the checked world region; visual inspection found no new missing geometry or
texture-layout change in this scene. This is not whole-game visual validation.

`tests/pt_rr_workgroup_check.py --cc ...` compiles the production selection
helper and verifies limits, fallback and exactly-once pixel coverage at all four
sizes, including partial edge groups. Its `--runs ...` mode checks matching
profiles, binaries, camera endpoints, script, completion and captures, and
labels timed-out results diagnostic-only. `tests/run-pt-performance.ps1` accepts
`-BenchmarkRRRows` without altering the user's saved settings. The release build,
combined-transport checks and 44 mocked RR allocation/failure cases pass.

NR initialization is now deferred until the feature is actually prepared.
Signed-runtime/export discovery still runs at startup; NR-off and RR-bypassed
paths do not initialize the snippet or allocate its capability parameters.
`tests/pt_nr_lazy_check.py` exercises the production lifecycle, including
first-use/reuse, disabled mode, failure unwinding and exactly-once shutdown.
This change did **not** resolve the Streamline shutdown timeout, and no steady
frame-rate improvement is attributed to it.

The `nr-first-use-20260909` validation smoke verified feature creation and
released it without validation messages, but its initial eight-frame script
quit before gameplay and did not establish evaluation. The revised 64-frame
`nr-first-use-debug-20260909` smoke recorded successful **NR evaluation** and
resource release. Neither is a clean exit pass. The latter started SDK shutdown
at 35.0 seconds; its owned-child debugger snapshot at 40 seconds shows the main
thread waiting inside `NvTelemetryAPI64!UninitializeTelemetry`, called from
NGX Vulkan shutdown via `slShutdown`. A worker is waiting in
`NvTelemetryBridge64!NvTelemetrySendEvent`. Module/export names identify the
wait path, not the exact proprietary variable/function responsible for it.
The cause of that worker wait remains unresolved; no NVIDIA service, system
telemetry policy, DLL or driver setting was changed. The independent watchdog
closed the child at 45 seconds. Evidence: ignored
`build-widescreen/rt-audit/nr-first-use-debug-20260909.stack.log` and its isolated
profile's `qconsole.log`. This diagnostic is not a timing comparison or proof
that every earlier RR timeout has the same internal cause.

### Idle-GPU retry with HD textures (2026-09-09)

The combined-radiance optimization is restored in source, generated shaders,
and the active renderer. Before testing, the RTX 5070 was at 0-1% GPU utilization
with roughly 300 MiB resident; the background texture-upscaling work was finished.
Saved settings and the newly generated `baseq3/z-vq3-hd.pk3` were not modified.

The first old-renderer run failed before loading the map. Vulkan validation
identified `VUID-vkCmdCopyBufferToImage-pRegions-00171`: a 2048x2048 RGBA texture
needed 16 MiB but the source buffer was only 8 MiB. `vk_image.c` now grows its
staging buffer for the full upload/mip chain, including cinematic uploads, and
drains previous users before replacing it. The adjacent image allocator's fixed
eight-entry table is now growable; allocation sizes, alignment and compatible
memory types are retained, including images larger than the usual 64 MiB chunk.
This does not change the existing 2048 texture-size cap or any quality setting.
`tests/vk_texture_memory_check.py` compiles the production allocation functions
and covers staging growth/reuse, 45 chunks, large images, memory types and cleanup.

Both comparison binaries include these same HD texture fixes. The separated
baseline was built by `tests/build-pt-rr-separated-baseline.ps1` from the backed-up
pre-optimization integrator/host routing and the current remaining objects.
The normal combined build remains installed; the comparison binaries are preserved
under `build-widescreen/rt-audit/rr-lean-idle-baseline-build-b-20260909/`.

Matched q3dm6 rendering captures (`rr-lean-hd-before-20260909` and
`rr-lean-hd-after-20260909`) use 1920x1080 DLAA, RR, flat two samples,
the user's current two bounces, x16 filtering, exposure 5, sharpness 0.7,
and FG/NR off. Camera start/end is identical at (-1361, 200, 77), yaw 0.
Each has 47 profile records, with three records trimmed from each end:

| Rendering measurement | Separated baseline | Combined RR |
| --- | ---: | ---: |
| Median tracing | 14.000 ms | 11.951 ms |
| Median engine frame | 23 ms | 20 ms |
| Equivalent rendered FPS | 43.5 | 50.0 |

These are **provisional rendering-phase observations**, not accepted end-to-end
performance passes. Both captured the full scenario and screenshot but reached
the 45-second guard during shutdown, after the Neural Rendering module-reference
release message. That shutdown fault also affects the baseline and remains
unresolved. No additional frame-rate improvement is claimed from generated frames.

Separate fixed-baseline and combined-RR validation runs loaded the validation
layer, rendered RR successfully and left empty validation logs; both also reached
the shutdown deadline. CPU transport checks and 44 mocked RR resource/fallback
failure cases pass. Side-by-side captures retain the textures and illumination;
world RGB mean absolute differences are 0.136/0.089/0.131 out of 255. This is a
single scene, not broad moving-material or whole-map validation.

### Combined-radiance transport trial (2026-09-08)

On 2026-09-09 the user identified a simultaneous GPU texture-upscaling model
as the source of the reported sub-1-FPS slowdown. That report therefore does
not establish a renderer regression. The briefly rolled-back optimization is
retried after confirming the GPU was idle (results above). Earlier timings below remain
historical, not acceptance evidence. The new comparison uses a noclip camera
and requires identical start/end positions, identical test scripts, matched
saved settings and clean completion within each 45-second run.

`pt_rr_trace.comp` now enables `PT_RR_COMBINED`. Its transport helper carries
one RGB path weight and one RGB radiance result instead of separate diffuse,
reflection, transmission and emission weights/results. It preserves the original
first-interaction diffuse/specular clamp, reflection/transmission/volume event
classification, roulette throughput, light sampling, RNG calls, material
programs, sample budget and bounce limit. Arithmetic is reassociated, not
bit-identical. Nonfinite paths are rejected before sample averaging; unlike the
native channel-separated fallback, rejection covers the whole combined path.

The tracer writes `rrNoisy` directly and updates optional adaptive moments from
the same fresh, unexposed color through `pt_rr_output.glsl`. At fixed sampling
the adaptive update is disabled. It no longer writes and rereads four vec4
radiance buffers (128 bytes/pixel of logical buffer traffic, approximately
253 MiB at 1920x1080). This is avoided traffic, **not freed VRAM or measured
DRAM bandwidth**: buffers remain allocated for native fallback. RR's albedo,
normal/roughness, depth, motion and reflection-distance guides are unchanged.

Native/staged/non-specialized rendering still packs its four outputs. The
post-trace barrier and saved push constants remain even when packing is skipped,
including for the failure-only HDR-to-display pass. Profiling explicitly reports
zero for the removed RR packing/filter intervals; adjacent timestamps without
intervening GPU work had exposed unsigned timestamp underflow in the first
candidate diagnostic. Other pass measurements are unchanged.

Verification:

- `tests/pt_rr_lean_check.py --cxx ... --sdk ... --baseline ... --gpu` executes
  3.2 million production GLSL transport events with each of normal and fast-math
  C++ compilation. Maximum error divided by max(1, signal magnitude) was below
  0.0000008. Cases include emission, first-lobe clamping, attenuation, glass/metal
  routing, roulette throughput, surviving fog scatters and ambient fill.
- The headless GPU fixture executes both actual transport helpers on the RTX
  5070 across 72 cases (1x1, 17x13 and 1920x1080). Every result passed its
  0.000002 absolute + 0.00001 relative tolerance. A deliberately corrupted
  negative control must fail readback. These are arithmetic tests, not a
  whole-scene or NVIDIA reconstruction equivalence proof.
- The native compact shader preprocesses to the exact pre-change token stream.
  Material/terrain/monitor/hologram/shell regressions, sampling checks, and 44
  mocked RR creation/binding failure cases pass. All 28 production shaders
  compile and pass SPIR-V validation.

**Performance remains provisional.** The matched `rr-lean-before-final-20260908`
and `rr-lean-candidate-20260908` captures use saved 1920x1080 DLAA, RR, flat two
samples, four bounces, x16 filtering, exposure 5, sharpness 0.7 and FG/NR off.
Their completed timing sections have 25 observations after trimming three at
each edge: tracing medians 24.608 / 22.408 ms, engine frame medians 33 / 30 ms.
However, both hit the independent 45-second deadline during shutdown, and
their matching camera trajectories were still settling for part of timing.
They are **diagnostic only**, not an accepted FPS improvement. Earlier longer
and shortened baseline attempts also timed out; none are counted as passes.

Inspected final-camera captures preserve the scene's textures, illumination and
HUD. Excluding top/bottom HUD regions, RGB mean absolute differences were
0.098/0.069/0.119 out of 255. This is one static view, not a moving optics/fog
quality certification. Saved game/shared profiles were unchanged and all game
processes were closed by their independent guards. A clean bounded end-to-end
comparison and broader moving-scene verification remain outstanding.

### Earlier four-/two-/one-sample comparison

RTX 5070, 1920x1080 input/output, four bounces, x16 filtering, saved exposure and
sharpness, FG and NR off. Same fixed q3dm6 camera, 41 settled profile records per
run after discarding three records at each edge. Short stationary measurements,
not an entire-map average or frame-generation presentation rate:

| Reconstruction | Samples/pixel | Trace time | Median frame | Real FPS |
| --- | ---: | ---: | ---: | ---: |
| Native + DLAA | 4 | 36.482 ms | 44 ms | 22.7 |
| RR + DLAA | 4 | 35.996 ms | 44 ms | 22.7 |
| RR + DLAA | 2 | 19.936 ms | 28 ms | 35.7 |
| RR + DLAA | 1 | 9.516 ms | 17 ms | 58.8 |

RR alone did not improve the four-sample frame median. The higher FPS comes from
explicitly tracing fewer samples and reconstructing them. The RR/post stage cost
about 5 ms at native 1080p. The `temporal` timing label is the HDR packing pass
when RR is active; native `spatial` time is zero.

Evidence under ignored `build-widescreen/rt-audit/`: profiles
`performance-rr-dlaa-{native,four,two,one}-a-20260908`, matching
`pt-perf-rr-dlaa-*.run.json`, settings/hash manifests, logs and stationary,
immediately-after-turn and settled screenshots. Earlier `rr-validation-a` and
`rr-native-four` runs used saved Quality mode and are **not** part of this DLAA
comparison. The user explicitly requested DLAA for all subsequent comparisons.

The DLAA two-sample lifecycle run (`rr-dlaa-lifecycle-a-20260908`) completed in
12.1 seconds: a renderer restart, native diagnostic rendering, RR resumption and
normal shutdown. Khronos validation was confirmed loaded; its warning/error log
was empty. This establishes this tested FG-off lifecycle, not all FG-on or
driver/device-loss scenarios.

The separate two-sample optical fixture run (`rr-dlaa-optics-a-20260908`)
completed in 15.4 seconds, including two renderer restarts, glass/water volumes,
normal/roughness materials, camera movement, a moving reflected target and its
removal. RR remained active, the target cleared from the reflection after
removal, and both the validation file and callbacks across all console lifetimes
were clean. Captures show working refraction/reflection, not a comprehensive
temporal-quality pass; their emissive checkerboard clips at the saved high
exposure, and low-sample reflected/refracted detail remains soft.

## Verification and limits

`tests/pt_ray_reconstruction_check.py` checks input routing, menu/build wiring,
matched benchmark manifests and optional live validation evidence. With `--cc`
and `--sdk`, it compiles the real RR resource implementation against a mocked
driver: every creation/binding failure point, missing format/memory/capability,
evaluation failure and repeated shutdown. This is not a substitute for visual QA.

The native workgroup check still verifies unchanged transport math. The SDK-free
Streamline stub also compiles. GPU fixtures are opt-in and use the independent
45-second process guard; no test is left running after completion.

Stationary and turning q3dm6 captures retained material textures, output exposure
and crisp HUD rendering. Fine textures can soften and trail during motion at low
sample counts. No claim of identical quality to four samples is made. The
reflection-distance guide uses a deterministic perfect-reflection query as an
approximation for rough lobes; it does not yet provide explicit specular object
motion vectors. Dielectric guides describe the first physical interface rather
than an entire nested transmission path. Optional separate transparency guides
are not implemented, so particles and complex glass/water motion still require
broader scene testing and guide refinement. This is a working RR integration,
not completion of every outstanding path-tracer material/reconstruction issue.

## RR sampling and redundant-work removal (2026-09-08)

Implemented in native Vulkan/GLSL, with no PK3 or new third-party runtime.

| Control | Default | Behavior |
| --- | ---: | --- |
| `r_dlssRayReconstruction` | 1 | Request NVIDIA RR; requires supported GPU/SDK, path tracing and DLSS/DLAA. Restart to change. |
| `r_pathTracingLightReuse` | 1 | Temporal + spatial RIS for primary-surface **map lights**. |
| `r_pathTracingAdaptive` | 0 | Optional: prior-frame variance, geometry and motion can halve per-pixel samples. Off keeps a fixed count. |
| `r_pathTracingSamples` | 2 | Fixed samples per pixel by default; a maximum only when adaptive sampling is explicitly enabled. |
| `r_pathTracingAdaptiveDebug` | 0 | Temporary diagnostic: green = half budget, red = full budget. |

The two sampling controls apply to the supported RR compact-transport path
(lighting mode 57), not reference/debug rendering or the staged prototype.
Switches, camera cuts, map loads, RR failures and renderer restarts reset history.
`pt_info` reports the actually enabled sampling flags and the sample ceiling.

### Removed work and memory ownership

RR no longer evaluates native changed-lighting/refractive-history guides or
writes their unused buffers. Its dedicated tracing shader no longer tone-maps
an intermediate image that RR never reads. A small fallback-only shader creates
that image if NVIDIA evaluation fails, before ordinary DLSS consumes it.

Reservoirs, geometry and variance history alias the existing native denoiser's
exclusive scratch/history storage. There is **no additional per-pixel VRAM
allocation** beyond the earlier RR images. Distinct current/previous descriptors,
compute barriers and mandatory history reset protect native/RR transitions.

### Light-sample reuse scope and estimator

This is a renderer-native ReSTIR-style implementation, **not the RTXDI SDK**.
Each visible surface gets two fresh map-light proposals from the existing cell
alias table (20% uniform support floor), plus a compatible previous reservoir.
History count is capped at eight candidates and expires after eight reuses. A
separate pass merges two nearby compatible temporal reservoirs. Spatial results
are never recursively fed back into temporal history. Light identities are
stable map-light indices; dynamic game lights require no index remapping because
they are deliberately not reused here.

The importance target has positive support across horizons/cones. Reuse updates
the target at the receiving surface and carries the source reservoir's normalized
weight and capped candidate count. The first primary path sample uses the result;
additional samples retain independent original selection. Every selected light
gets fresh visibility, including colored glass transmittance. No cached shadow
or radiance is substituted. Sun, emissive triangles, moving game lights and
secondary-bounce light sampling retain their existing estimators/MIS.

This scope adds effective map-light candidates, not fewer shadow rays for every
light category. It has a processing cost and is not a standalone FPS improvement
in the short four-sample q3dm6 comparison. Extending reuse to emissive/dynamic
lights and indirect paths is not implemented by this change.

### Adaptive sampling safeguards

The budget is selected **before** tracing, from reprojected prior-frame noisy
luminance moments, not early stopping on the current frame's samples. Radiance
is normalized by the actual count. At least half the ceiling is retained, rounded
up, with at least one sample. New/disoccluded pixels, uncertain or fast camera
motion, changing dynamic lights, tracked objects, animated/effect/decal materials,
glass and smooth/metallic reflections retain the full budget. High-variance
surfaces and histories younger than eight observations also retain full sampling.

NVIDIA still receives fresh HDR; the variance history is not blended into color.
The initial implementation only benefited settled views; the moving-history
extension below also permits reliably tracked camera pans, not rapid turns.
Lower sample counts remain a noise/quality tradeoff.

### Current measurements and validation

Saved 1920x1080 DLAA, four-sample ceiling, four bounces, x16 filtering, exposure
11.313708 and sharpness 0.5; FG and NR off. Same q3dm6 profile section, 41 settled
records per run. These results are separate from the earlier lower-exposure table.

| Configuration | Trace | Frame median | Rendered FPS |
| --- | ---: | ---: | ---: |
| Before this pass, fixed 4 spp | 36.714 ms | 45 ms | 22.2 |
| Adaptive only, initial implementation | 29.755 ms | 38 ms | 26.3 |
| Combined reuse + adaptive | 30.348 ms | 39 ms | 25.6 |

Combined stationary FPS improved about **15%**; most savings come from adaptive
sampling. The cleanup-only validation run did not establish an FPS gain, and the
combined validation-enabled timing is not substituted for the non-validation
performance run. No map-wide or moving-camera speedup is claimed.

Evidence: `performance-rr-sampling-{before,adaptive,combined-timing}-20260908`
under the ignored audit directory. The separate `combined` validation run also
captures actual sample budgets: stable areas reduce to half; the turning view
restores full sampling (47.15% half-budget pixels in the stationary capture,
0.00% during the turn, excluding the HUD strip). Textures, exposure and HUD placement were inspected in
the saved captures; this is not proof of identical noise or all-scene quality.

`pt_rr_sampling_check.py --cxx ...` executes the production reservoir and budget
math in a seeded CPU fixture (300,000 temporal/spatial trials), checks full-support
and scheduling contracts, and verifies mode-default wiring. The resource fixture
now tests 44 failing allocation/pipeline operations and asserts that failed RR
evaluation dispatches fallback tone mapping. Shaders pass SPIR-V validation and
the existing material-storage/workgroup checks.

The four-sample `rr-sampling-lifecycle-20260908` validation run completed in
15.1 seconds with a renderer restart, native diagnostic mode and return to RR.
The layer was confirmed loaded, warnings/errors and console callbacks were
empty, and saved settings were unchanged.

The final four-sample `rr-sampling-optics-20260908` validation run completed in
23.6 seconds, including two renderer restarts, glass/water, moving mirror targets
and target removal. RR stayed active, validation was clean, and saved settings
were unchanged. The optical fixtures still clip at the user's high exposure;
they verify transport/lifecycle coverage, not perfect optical reconstruction.

## Stationary-screen jitter correction (2026-09-08)

The renderer's projection adds `+2*jitter/extent` to its Z column. Because
`clip.w = -z`, this displaces the rendered image by **negative** jitter pixels.
The shared DLSS/RR bridge previously reported positive jitter to NVIDIA, so
reconstruction doubled the visible displacement instead of compensating it.
The bridge now reports `(-jitter_x, -jitter_y)`. Projection sampling, unjittered
camera constants and jitter-free motion vectors are otherwise unchanged.

Two consecutive-frame q3dm6 captures reproduced the fault and verified the
sign-only fix at identical saved 1920x1080 DLAA settings. RR, adaptive sampling
and light reuse remained enabled; FG and NR were off. Each capture contains
16 stationary frames (two eight-frame jitter cycles), including the initial
capture transient. An optical-flow fit on two static texture patches measured:

| Static patch | Before X/Y peak-to-peak | After X/Y peak-to-peak | Before/after RMS |
| --- | ---: | ---: | ---: |
| Circuit texture | 1.598 / 1.323 px | 0.253 / 0.155 px | 0.685 / 0.068 px |
| Wall texture | 1.288 / 0.504 px | 0.537 / 0.104 px | 0.313 / 0.129 px |

These are fitted displacement estimates, not a claim of zero noise or perfect
reconstruction in every scene. The original repeating jitter cycle is absent
from the corrected sequence. The fixed run completed in 10.4 seconds with the
Vulkan validation layer loaded and no warnings/errors. Both runs closed
automatically, preserved saved settings and had stable reconstruction history.

Evidence: `performance-rr-jitter-{before,sign-fixed}-20260908` under the ignored
audit directory. `tests/pt_rr_jitter_check.py --compare BEFORE AFTER` checks
matched settings, active RR/DLAA, clean validation and displacement bounds.
Its offline tests lock the production sign/projection contract, demonstrate
the doubled-displacement negative control and calibrate the measurement on
known synthetic subpixel shifts. No quality/performance option was disabled
to address this bug.

## Moving adaptive sampling and shared post-processing (2026-09-08)

### Moving-history policy

Adaptive sampling now measures camera motion from **unjittered** projected
coordinates. History lookup still addresses the actual jittered prior image.
For movement of one pixel or more, a complete 2x2 prior-frame footprint must
match material/object identity, normals, roughness, position and surface plane.
All four taps must have at least 12 observations and relative variance at most
0.08 (stationary history retains its eight-observation / 0.15 variance gate).
Missing or incompatible taps never get discarded and renormalized away.

Fast motion of 32 pixels/frame at 1920-pixel render width, scaled with render
width, retains full sampling. So do glass, smooth/metallic reflections, tracked
objects, effects, decals, changing dynamic lights and invalid history. Unsafe
surfaces and fast movement clear adaptive evidence, so reductions need to mature
again. An uncertain but geometrically matching pan may accumulate observations
at full sampling until all four taps qualify. Only prior noisy-radiance moments
are combined; no old radiance is fed into NVIDIA's input.

The sample floor remains **two with a four-sample ceiling**. This change does not
enable one-sample regions, reduce the bounce count, alter DLAA or expand map-light
reservoirs to other light types. No extra images or history buffers are allocated.

### Post-processing math reuse

The 8x8 display/sharpening group now shares a 10x10 tone-mapped tile, including
its one-pixel halo. This performs 100 color conversions instead of 320, using
1,200 bytes of group-local storage without an additional full-screen pass.
With sharpening off, the existing single-read path remains. Out-of-bounds lanes
participate in the shared-memory barrier before returning, including partial
edge groups. Exposure, tone curve, sharpening and debug-overlay math are unchanged.

`tests/pt_rr_post_check.py --gpu --cxx ... --sdk ...` compiles the frozen former
shader and runs it against the production SPIR-V on identical inputs. All **72
GPU cases were byte-identical**, covering 1x1, 17x13 and 1920x1080, exposure
0.01/1/11.313708/16, sharpness 0/0.5/1, budget overlay on/off, negative HDR,
subnormals, NaN and infinity. Core/synchronization validation was clean. This
headless fixture has a 25-second timeout and neither opens a game nor changes
settings. The CPU tile test independently checks halo ownership and edge math.

Separate game lifetimes are not pixel-identical because reconstruction/history
can vary. The post-only game comparison stayed below one mean channel level out
of 255 in all eight lossless captures. That broad image guard is not substituted
for the strict same-input GPU test.

### Measured results and limits

Matched saved 1920x1080 DLAA, RR, four-sample ceiling/four bounces, x16 filtering,
exposure 11.313708, sharpness 0.5, FG/NR off. The q3dm6 scenario settles at
(-1395, 200, 50), then pans at 15 degrees/second with fixed game time. Stationary
and pan phases retain 25 and 33 GPU observations after trimming three at each
edge. The jitter fix is present in every baseline and candidate below.

| Run (`rr-motion-...-20260908`) | Stationary frame | Pan trace | Pan frame | Pan rendered FPS |
| --- | ---: | ---: | ---: | ---: |
| `before` | 39 ms | 36.937 ms | 46 ms | 21.7 |
| `post` only | 39 ms | 37.342 ms | 46 ms | 21.7 |
| `timing` combined | 41 ms | 33.579 ms | 42 ms | 23.8 |
| `confirm` combined | 39 ms | 33.748 ms | 42 ms | 23.8 |

Both normal combined runs improved the pan frame rate by **9.5%**, with tracing
about 8.6–9.1% cheaper. Stationary results varied from 39–41 ms; no stationary
speedup is claimed. The post-only stage dropped roughly 0.02–0.03 ms in this
comparison with no frame-median gain. Its roughly 5-ms timing label includes
NVIDIA reconstruction and is not entirely display/sharpening work.

The separate `after` validation run completed in 15.3 seconds: its layer was
confirmed loaded, validation file/console callbacks were clean, and saved
settings remained unchanged. Budget captures show about 47.2% reduced sampling
stationary, **26.2% while panning**, and **0% during the fast turn and following
camera-cut capture**. Tests exercise these actual debug outputs rather than
inferring budget coverage from FPS. Pan captures were inspected for retained
textures, HUD placement and lighting; no all-scene or identical-noise claim is
made for adaptive sampling. Gameplay windows for these comparisons lasted
14.1–16.6 seconds each, protected by independent 45-second guards.

Evidence lives under ignored `build-widescreen/rt-audit/performance-rr-motion-*`.
`tests/pt_rr_motion_check.py BEFORE VALIDATION AFTER --accept` enforces matched
quality/configuration/cameras, successful RR, validation and budget safeguards.
The production sampling fixture also tests plane/normal/identity/roughness
rejection, age/variance/fast-motion gates and the unchanged two-to-four budget.

The final `rr-motion-jitter-20260908` validation capture completed in 10.4 seconds
and passed the established absolute jitter bounds (circuit/wall fitted RMS
0.069/0.130 px over all 16 frames). It is not a new A/B against the earlier
jitter-baseline run: the saved configuration source hash changed between tasks,
and the comparison checker correctly refused that pairing. Use
`pt_rr_jitter_check.py --accept RUN` for this standalone regression check.

## Primary implementation references

- [NVIDIA Streamline DLSS-RR programming guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md): resource semantics, options, albedo approximation and lifetime.
- [NVIDIA Vulkan DLSS-RR sample](https://github.com/nvpro-samples/vk_denoise_dlssrr): Vulkan HDR/guide integration example.
- [Spatiotemporal Reservoir Resampling](https://research.nvidia.com/labs/rtr/publication/bitterli2020spatiotemporal/): reservoir resampling method.
- [NVIDIA RTXDI integration guide](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/Integration.md): separation of primary direct-light, indirect-light and path reuse.
