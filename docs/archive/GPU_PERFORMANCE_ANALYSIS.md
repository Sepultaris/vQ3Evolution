# GPU performance diagnosis — 2026-09-08

Historical hardware analysis of the **pre-Ray-Reconstruction** renderer.
The state/register follow-up below was subsequently implemented, and RR plus
combined-radiance tracing changed the active workload. These counters and old
"next target" statements must not be presented as a fresh profile of today's
RR shader. See [current status](../STATUS.md) and the
[later RR measurements](RAY_RECONSTRUCTION_DEVELOPMENT.md).

## Implemented follow-up: workgroup register budget

The accepted follow-up changes scheduling, not ray count or shading. The compact
integrator specializes its workgroup height: NVIDIA uses 8x128 when hardware
limits permit, otherwise 8x64; other vendors retain 8x8. Pipeline creation retries
64 and then 8 rows on failure. Dispatch dimensions use the successfully created
size. The per-pixel shader body and random stream are unchanged; no extra path
buffers or material function calls were introduced.

RTX 5070 compiler statistics changed from 255 registers/thread (8x8) to 128
(8x64) and 64 (8x128). Both larger variants used 49,152 bytes of driver-reported
shared memory. These resource counts alone are not proof of speed; the following
normal, uninstrumented comparisons supply that evidence.

All runs below used saved 1920x1080 DLAA, 4 samples, 4 bounces, 16x filtering,
exposure 5.656854, temporal/spatial reconstruction, and **Frame Generation off**.
Source config hash: `151E0CE38F63D035B41AB983BDCE1CBBB470F0046B8887D3D0B732FC6C272561`.
The user's saved FG setting remained unchanged; only isolated test profiles
overrode it. Neural Rendering was off except for the explicitly named cost test.

| Run suffix (`performance-...-20260908`) | Trace median | Post median | Real frame median | Approx. real FPS |
| --- | ---: | ---: | ---: | ---: |
| `query-base-c` — original | 55.540 ms | 0.680 ms | 62 ms | 16.13 |
| `query-group-b` — 8x64 prototype | 38.306 ms | 0.685 ms | 45 ms | 22.22 |
| `query-final` — integrated 8x64 repeat | 38.331 ms | 0.685 ms | 45 ms | 22.22 |
| `query-groupwide` — 8x128 | 35.870 ms | 0.697 ms | 43 ms | 23.26 |
| `query-nron` — original, NR on | 55.636 ms | 6.351 ms | 68 ms | 14.71 |

Each has 49 settled GPU timestamp observations, successful independent process
guard exit, and matching before/after camera (-1395, 200, 50), yaw 0. The test
waits for connection before teleporting; Quake's teleport launch velocity causes
the requested x=-1510 to settle at -1395. An earlier wrong-spawn run is excluded.
Use `tests/pt_query_state_check.py <profile-directory> [...]` to enforce acceptance.
Logs and complete manifests are in the ignored `build-widescreen/rt-audit` tree.

The retained 8x128 result reduces tracing time by **35.4%**, and increases the
reciprocal median real-frame rate by **44.2%**. This is a short q3dm6 result,
not a map-wide FPS guarantee or a claim that overall performance is finished.
The four NR-off screenshots above are byte-identical, SHA-256
`66442002C6FA18B40F81BBDAC3177B621ED2FEB41204FAF34C18A239DA1D3E27`.
The NR comparison isolates roughly **5.67 ms** of additional post-processing;
it is not counted as part of the shader optimization's gain.

The final release build passed Vulkan synchronization/core validation with
camera movement and compact/fallback pipeline transitions in 7.8 seconds.
`pt-perf-query-validation-20260908.run.json` records the validation layer loaded,
normal exit, completion marker and unchanged saved settings; its validation log
is empty. Offline checks include 73,728 production pipeline lifecycle cases
(device limits, all fallback levels, partial failures and cached retries),
2.4 million transport events under each arithmetic mode, exact preprocessed
shader-body comparison, and partial-edge dispatch coverage.

Rejected during this pass: outlining rare candidate-material callbacks did not
lower the 255-register ceiling; outlining only `layerSampleUV` lowered it to 128
but made the tested tracing workload slower. Moving lighting calculations ahead
of visibility and explicitly storing path state in shared memory also did not
lower the compiler's register ceiling. These source experiments were removed;
they are not enabled or presented as performance wins. Failed/early scenario
runs are diagnostic only, not included in the accepted table.

The original hardware diagnosis below describes the **pre-change** renderer.

## Original capture finding

The normal optimized path tracer is a **register-limited, latency-bound compute
shader**, not a renderer saturating the GPU's arithmetic or RT-core throughput.
The shader keeps too much state resident to run enough independent warps while
ray queries and dependent loads complete. Divergence further reduces useful work
inside those warps. This is an implementation bottleneck worth addressing before
more filtering, light-selection arithmetic, or presentation tweaks.

This is a diagnosis, **not a new performance improvement**. No game was launched,
no quality settings were changed, and no renderer code was modified for this
analysis. The computer-use skill was used to open the existing native trace and
export its shader table; the viewer was closed afterward.

## Evidence and settings

GPU: RTX 5070 / GB205. Saved quality: 1920×1080 DLAA, four samples, four bounces,
16× anisotropy, temporal and spatial reconstruction enabled, Neural Rendering
enabled, exposure 4. **Frame Generation was off**, not on. Saved-config SHA-256:
`D73E3E5933A250C40582A15E2BBEE82A0AED60F0D5F4B7773B33860947FBEB86`.

All local evidence below is relative to `build-widescreen/rt-audit/`:

- Systems: `performance-nsight-systems-labels-20260908-a/nsight/systems.sqlite`,
  game PID 27672, 47 game-owned tracing annotation windows.
- Graphics: `performance-nsight-graphics-setup-20260908-g/nsight/vQ3Evolution_2026_09_08_03_09_36.ngfx-gputrace`.
  The viewer analyzed 205.13 ms of this nominal 250-ms capture. The visible first
  queue submission took 67.36 ms. This is a short, stationary-scene diagnostic,
  not a map-wide or moving-scene performance acceptance test.
- Exported shader table: the same Graphics directory, `shader-pipelines.csv`.
- Reproducible summary: `nsight-analysis-20260908.json`.
- Independent earlier engine timestamp logs:
  `pt-perf-optimization-pass-combined-a-20260908.log` and
  `pt-perf-optimization-pass-combined-b-20260908.log`.

## Which work dominates?

The native shader profiler attributes 54,959,503 samples as follows. These are
**shader sample shares, not exclusive frame-time percentages**:

| Shader/group | Sample share | Registers/thread | Maximum resident warps/SM |
| --- | ---: | ---: | ---: |
| Main compact path tracer | 83.98% | 255 | 8 |
| NVIDIA SDK shaders | 9.10% | Not exposed | Not exposed |
| Spatial reconstruction | 3.74% | 64 | 32 |
| Temporal reconstruction | 1.54% | 80 | 24 |
| Surface guides | 1.51% | 255 | 8 |
| Unattributed | 0.060% | — | — |

The main shader is `pt_compact_transport.comp`, hash `0x8b94a7e3eafd372a`, with
an 8×8×1 group and 12,288 bytes of shared memory per group. The hardware supports
48 resident warps/SM; the compiled shader permits only eight (16.7%). The register
requirement explains this ceiling: four 64-thread groups consume approximately
the 65,536-register SM budget, before a fifth group could fit. This is not merely
a guess based on a task-manager GPU utilization percentage.

Earlier non-Nsight A/B timing logs independently place the retained optimized
tracer at **55.706–55.847 ms median** across four settled phases (41 observations
per phase, dropping the first six). Temporal plus spatial reconstruction totals
about 3.5–3.6 ms; NVIDIA/post composition about 6.34–6.37 ms; guides about
1.05–1.08 ms; acceleration structures about 0.41 ms. Engine frame medians are
68–69 ms. Shader sample shares are not substituted for these GPU timestamps.

## Why the tracing shader is slow

Systems hardware counters were evaluated inside the middle 50% of each tracing
annotation window: 1,604 readings per counter, sampled almost exactly every
millisecond. Startup/compilation and the adjoining post-processing tail are
excluded. Middle-80% results provide a sensitivity check.

| Counter | Middle-50% mean | Middle-80% mean |
| --- | ---: | ---: |
| Graphics engine active | 99.99% | 99.99% |
| Compute register allocation | 94.97% | 94.39% |
| Compute warp occupancy | 15.96% | 18.06% |
| SM instruction-issue throughput | 17.18% | 17.86% |
| RT-core throughput | 24.33% | 22.66% |
| VRAM bandwidth throughput | 1.03% | 4.58% |
| L1/texture subsystem throughput | 11.95% | 13.81% |
| Predicated-on threads per 32-thread warp | 14.68 | 15.52 |

Threads launch at 32/warp but only about 15 participate at a typical instruction
in the selected intervals. The GPU is occupied with work while most potential
parallel capacity is unavailable. Low aggregate bandwidth does **not** mean
memory accesses are cheap: dependent cache hits and ray results still have
latency, which this low-occupancy shader struggles to hide.

The main shader's 46,157,276 samples break down into:

- Long scoreboard/data-dependency waits: **50.90%**.
- Coupled arithmetic dependency waits (`WAIT`): **17.84%**.
- No instruction available (`NOINST`): **12.71%**.
- Selected/issuing an instruction: **8.63%**.
- Math-pipe throttle: only **0.15%**; texture throttle: **zero samples**.

These distinguish latency/dependencies from simply having too much arithmetic
for the chip. NVIDIA defines the register/warp columns and dependency-producer
attribution in its [Shader Profiler guide](https://docs.nvidia.com/nsight-graphics/UserGuide/shader-profiler.html).
`NOINST` includes instruction-fetch scheduling and cache misses; it does not by
itself prove a particular cache miss rate. See NVIDIA's
[stall definitions](https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html).

The initial whole-trace **Input Dependencies** view ranked these producers:

| Producer | Dependency-attributed samples |
| --- | ---: |
| RT-core/ray tracing | 127,783,335 |
| Global buffer loads | 22,749,178 |
| Texture fetches with derivatives | 2,230,034 |
| Local-memory loads | 953,846 |
| Special functions | 899,256 |

Those are the five visible rows transcribed from the viewer, not a complete
export or a main-shader-only breakdown. Dependency attribution can count a
sample through multiple dependencies; **do not sum these into frame percentages
or equate them to the long-scoreboard percentage**. They establish the dominant
producer ranking, not the exact time spent in individual GLSL calls. In
particular, the evidence does not support the earlier idea that local-memory
spilling is the dominant cost.

## What is not the primary target

- **CPU submission:** during the 3,286.25-ms selected frame span, the union of
  game `vkWaitForFences` calls occupies 3,215.35 ms (97.84%). The CPU is waiting
  for rendering, not spending most of the interval preparing the next frame.
- **Cold compilation:** a 6.866-second compute-pipeline creation occurred in the
  Systems capture, but outside this sustained-rendering selection. It explains
  a startup pause, not the continuing low frame rate. Whole-capture averages
  would misleadingly mix it with gameplay.
- **VRAM bandwidth or texture-filter saturation:** neither is near saturation
  in the selected tracing interiors. That does not rule out expensive cached
  loads, divergence, or post-processing bandwidth elsewhere in the frame.
- **Presentation/FG:** FG was disabled. This evidence does not make FG or an
  inactive FG hook the explanation for the main shader's execution stalls.
- **Denoising/DLAA/NR:** real secondary costs, but eliminating all of the roughly
  10-ms reconstruction/post work would still leave a roughly 56-ms tracer.

## Concrete next implementation target

Target the **state carried across traversal and light-visibility queries** in
`shaders/pt_integrator.glsl` (`trace`, `visibility`, `integrator`) and the shared
visibility call in `shaders/pt_light_loop.glsl`. The current invocation carries
path/radiance weights, material and BRDF data, optical-medium state, and
light-selection variables across dependent ray work. The saved source confirms
that organization; this list is **not** a source-line stall ranking.

First reduce live state and unnecessary lifetime overlap around those queries.
If splitting work is required, prototype a narrow traversal/visibility kernel
with compact records and separate uncommon material handling. Preserve sample
counts, bounces, materials and the estimator. Do not merely re-enable the rejected
staged prototype: its wide state transfers and unchanged expensive kernels were
already measured slower.

Acceptance must show lower compiled register demand, more effective occupancy
and less exposed dependency latency, **then lower actual frame time at the same
saved settings**, with image comparison. Lower registers alone can introduce
spills and regress performance. No FPS target or speedup is established yet.

## Limitations and reproducibility

Systems warns that some Vulkan events may be missing; counters are device-wide.
Its temporal/spatial/post debug-label spans overlap the tracing span and can be
near zero despite those shaders demonstrably executing. They are selection
windows, **not valid exclusive pass timings**. This analysis uses central-window
counters, independent engine timestamps and the native shader report together.

The native report resolves GLSL/SPIR-V sources, but reports 13 shaders missing
function-level information for nested call graphs. Exact high-level stall
producer lines and variable-level register attribution were not exported in
this pass. The shader-level diagnosis is established; a particular variable
rewrite or a choice between primary and shadow traversal is not yet proven.

The CSV exporter has misaligned middle headings. The analysis ignores those
columns and reconstructs sample shares from the fixed 17-state suffix, matching
every row against the exported Samples field. Systems float counters are stored
as IEEE bit patterns in INTEGER fields; the reader decodes their declared type.
Counters requiring display multipliers are intentionally excluded.

```powershell
python tests/pt_nsight_analysis_check.py
python tools/pt-analyze-nsight.py --systems build-widescreen/rt-audit/performance-nsight-systems-labels-20260908-a/nsight/systems.sqlite --pid 27672 --shaders build-widescreen/rt-audit/performance-nsight-graphics-setup-20260908-g/nsight/shader-pipelines.csv
```

The analyzer opens SQLite read-only, records evidence hashes, launches nothing,
and prints JSON. An optional `--output` refuses to overwrite an existing file.
