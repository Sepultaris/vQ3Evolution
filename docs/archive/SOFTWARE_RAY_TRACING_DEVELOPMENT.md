# Software path-tracing development record

Historical measurements and regression results from 2026-09-12. These describe
the recorded builds and settings, not current whole-renderer performance.
See [the software guide](../RTX.md) and [current status](../STATUS.md).

## Internal breakdown, 2026-09-12

These measurements precede the direction-ordered traversal change below.

RTX 5070, Q3DM6, camera `(-115 448 50) : 0`, 1920x1080 output and 960x540
tracing, fixed two samples, six maximum bounces, NRD and saved filtering/
exposure/bloom settings; DLSS, FG and Neural Rendering off. Each diagnostic
measured 32 phases and 256,320 selected pixels, averaging 2.912 bounce iterations
per path. This is one scene and partial phase coverage.

| Timed diagnostic category | Sampled clock share |
| --- | ---: |
| Visibility BVH traversal | 27.66% |
| Ordinary/decal BVH traversal | 23.86% |
| Weapon BVH traversal | 2.48% |
| Path continuation | 13.62% |
| Trace callback/control | 11.80% |
| Point/sun setup | 7.60% |
| Emitter sampling | 4.74% |
| Material/texture evaluation | 3.41% |
| Visibility callback/control | 3.28% |
| Remaining categories | 1.55% |

| Query kind | Queries per traced pixel | Nodes/query, timed | Nodes/query, counters-only |
| --- | ---: | ---: | ---: |
| Ordinary/decal | 5.752 | 90.00 | 90.37 |
| Visibility | 11.024 | 44.11 | 44.12 |
| Weapon | 2.001 | 93.83 | 91.11 |

Across query kinds, the two runs measured about **18.78 queries, 1,188-1,192
visited nodes and 135.5-135.8 triangle tests per traced pixel**. Total query
counts differed by less than 0.001%, total nodes by 0.27%, and total triangle
tests by 0.22%. They are not bit-identical: weapon nodes differed by 2.90% and
weapon triangle tests by 4.88%. Separate live runs do not establish identical
dynamic scenes or explain every count difference.

The evidence points toward geometry-query/traversal work for further inspection,
especially visibility queries. It does **not** identify an already-proven faster
algorithm, establish exact production category times, or claim a speedup from
this instrumentation. Roughly 54% is traversal's share of the instrumented
clock samples, not 54% of the normal frame that can necessarily be eliminated.

Local evidence under `build-widescreen/software-lighting-audit/`:

- Timed `run-8ee5bcbda7194b9cabfc6c988db5579a`: normal exit in 48.219 seconds.
- Counters-only `run-42823b01fbe6417fbf2b2f25bb2bfaf1`: normal exit in 53.156
  seconds with an empty synchronization-validation log and zero clock ticks.
- Diagnostics-off `run-366de85f7c3b4dcb8e4577978ba50610`: normal exit in
  39.875 seconds, empty validation log, and no diagnostic initialization/replay
  records. Its validation-on timings are not a performance comparison.
- Initial timed `run-8cadf7a2e21643de8e388514d5c8dde8`: rendering and logging
  finished with no validation messages, but the 45-second guard stopped device
  shutdown. Retained as an incomplete test, not counted as a normal-exit pass.

The normal software/hardware embedded shader payloads matched the pre-diagnostic
backup byte-for-byte, and hardware scene dispatch was unchanged. Saved user
settings were unchanged. Game tests exercised the NRD diagnostic variants;
native variants passed compilation, payload and write-isolation checks but were
not separately exercised in a live game during this diagnostic pass.

## Software traversal comparison, 2026-09-12

RTX 5070, Q3DM6, verified camera `(-115 448 50) : 0`, 1920x1080 output,
960x540 tracing (`r_pathTracingScale 0.5`), fixed two samples, six saved bounces,
NRD on, 16x anisotropic filtering and saved exposure/bloom settings. FG, NR and
DLSS were off. Each accepted measurement contains 32 frames; all runs exited
normally within the guard. These are instrumented, single-camera measurements,
not a map-wide or older-GPU FPS guarantee.

| Build/run | GPU median | Trace median | Guides median | Wall median |
| --- | ---: | ---: | ---: | ---: |
| Instrumented original traversal A1 | 152.228 ms | 147.508 ms | 3.304 ms | 153.954 ms |
| Reciprocal box traversal B1 | 129.548 ms | 125.653 ms | 2.421 ms | 131.198 ms |
| Instrumented original traversal A2 | 152.848 ms | 148.164 ms | 3.314 ms | 154.704 ms |
| Final build B2 | 120.741 ms | 116.982 ms | 2.426 ms | 122.372 ms |

Both optimized runs were faster, with roughly **15-21% lower GPU frame time**.
The spread between optimized runs is retained rather than presented as a
universal speedup. The final build also initializes mode-1 auto-exposure before
binding the tracing descriptor set. Validation caught its previous first-use
descriptor update after binding; moving initialization ahead of tracing removed
that error. The final separate synchronization-validation run completed in
42.000 seconds with an empty validation log; its timings are excluded above.

Native temporal filtering cost about 0.33 ms, NRD 0.90 ms and CPU scene
preparation 0.66 ms. No denoising pass was removed and no static-link cache was
added: the measured tracing cost was much larger. Samples, bounces, resolution,
material evaluation and triangle acceptance were not reduced. The four software
trace/guide payloads changed; mode-2 shader payloads and hardware scene-recording
code matched the pre-pass backup. Query correctness passed on NVIDIA and AMD,
including negative-zero, near-parallel rays and tight distance intervals.

Local evidence under `build-widescreen/software-lighting-audit/`:
A1 `run-fd8ce5f42581431285f434ed3a4eed0d`,
B1 `run-c077a083937946198175cd11bc70362e`,
A2 `run-bf32e216ecfe4e35849995bf91d0518f`,
B2 `run-2863cbb8cb3f47928349577672d2c042`,
final validation `run-e836a18db01f4aa0849507b02c6db79a`.
The earlier failed validation run `run-0bf48ef0fce1481596b2c2af1909807b`
is retained as failure evidence, not counted as a pass.

## Excluded static-tree skip, 2026-09-12

Mode 1 now starts weapon-only queries at the dynamic tree instead of visiting
static BSP nodes that cannot contain a permitted hit. The header's static mask
also handles decal-only and mixed-mask queries correctly. Triangle/material
acceptance, samples, bounces and shading are unchanged; no buffer was added.

A fresh A/B/A comparison used the same Q3DM6 camera and saved settings listed
above (including NRD, two samples, six bounces and 0.5 tracing scale). Each run
completed 32 measured frames, verified its camera before/after measurement and
exited normally in under 40 seconds. These are new baselines, not timings reused
from the earlier reciprocal-traversal comparison.

| Run | GPU median | Trace median | Guides median | Wall median |
| --- | ---: | ---: | ---: | ---: |
| Before A1 | 112.437 ms | 108.642 ms | 2.420 ms | 114.074 ms |
| Static-tree skip B | 111.255 ms | 107.851 ms | 2.034 ms | 112.892 ms |
| Before A2 | 112.554 ms | 108.834 ms | 2.388 ms | 114.167 ms |

The observed saving is **1.2-1.3 ms, about 1% of GPU frame time**, in this one
scene. This is a small improvement, not a solution to the remaining tracing cost.
The extended reference fixture passed 32,768 queries on each of RTX 5070 and
AMD integrated graphics, checking actual root selection as well as hits against
brute force. It retains arbitrary mixed-mask static trees and adds world-only
static trees, with aligned world/weapon geometry and weapon miss cases.
Mode-2 payloads and its scene-recording function matched the pre-pass backup.

Local evidence under `build-widescreen/software-lighting-audit/`:
A1 `run-9cc5ecbef5f64716b6b10f927df7d6bc`,
B `run-d0471d699b0e447a9528e8d4fbe6f9c6`,
A2 `run-3f7be88fc34e433d84562f3a03690d9c`.
Separate synchronization validation passed with an empty log in
`run-e8f3125411c74cb3b2b1663a1bf730e3`; its timing is excluded from the comparison.

## Direction-ordered traversal, 2026-09-12

The internal diagnostic identified geometry traversal as the largest sampled
category. Code inspection then found that every ray visited the left BVH child
first, regardless of direction. Mode 1 now selects one of eight precomputed
direction-sign orders, so it can discover a blocker or nearer accepted surface
earlier and prune later work. This changes traversal order, not the ray budget,
intersection equations, material callbacks, light selection or denoising.

A fresh A/B/A/B comparison used RTX 5070, Q3DM6, camera `(-115 448 50) : 0`,
1920x1080 output, 960x540 tracing, two samples, six maximum bounces, NRD and
saved filtering/exposure/bloom settings. DLSS, FG, Neural Rendering, shader
diagnostics and Vulkan validation were off during these timing measurements.
Each run verified its camera, measured 32 frames and exited normally in 37-39
seconds. These are normal-kernel GPU timestamps, not diagnostic clock shares.

| Run | GPU median | Trace median | Wall median | CPU scene preparation |
| --- | ---: | ---: | ---: | ---: |
| Before A1 | 111.511 ms | 108.074 ms | 112.998 ms | 0.671 ms |
| Ordered B1 | 79.502 ms | 76.104 ms | 81.204 ms | 0.716 ms |
| Before A2 | 111.522 ms | 108.102 ms | 112.935 ms | 0.649 ms |
| Ordered B2 | 79.713 ms | 76.337 ms | 81.451 ms | 0.780 ms |

The repeatable saving was **31.8-32.0 ms, about 29% of GPU frame time**.
Measured wall intervals correspond to roughly 8.9 to 12.3 rendered FPS, about
39% higher. This is still costly software tracing, not a 60 FPS claim or a
map-wide/non-RTX-GPU performance guarantee. Initial compilation checks ran
alongside B1/A2; B2 ran after those checks ended and retained the gain.

The order buffer grows from one to eight words per allocated node: about
9.33 MiB extra device-buffer capacity and the same increase in each of its
staging and CPU mirrors (about 28 MiB total requested storage across the three).
Static links are cached, so only their header and the dynamic links are uploaded
on ordinary frames. The CPU preparation change was under 0.14 ms in this test;
the improvement comes from the GPU tracing pass, not CPU offloading.

All eight orders passed full-node reachability checks. The production query
fixture passed 32,768 queries per device on NVIDIA and AMD, covering direction
octants, rejected alpha candidates, nearest/any-hit, masks, tight distance
intervals, barycentrics and empty/static/dynamic trees. Software shaders rebuilt
reproducibly, and diagnostic write-isolation checks passed. The before/after
screenshots were inspected without an obvious scene regression; this is not
exhaustive material or motion validation. Mode-2 scene dispatch and all
non-software shader payloads matched the pre-pass backup.

A separate counters-only run, with synchronization validation enabled, exited
normally in 49.719 seconds with an empty validation log. Compared with the
pre-change counters-only diagnostic at the same camera/32 phases:

| Work per traced pixel | Before | Ordered |
| --- | ---: | ---: |
| Queries | 18.777 | 18.776 |
| BVH node visits | 1,188.47 | 1,116.94 |
| Triangle tests | 135.54 | 122.54 |
| Non-visibility candidate callbacks, including weapon queries | 11.987 | 8.383 |
| Visibility candidate callbacks | 9.590 | 8.887 |

Ordinary/decal node visits per query fell from 90.37 to 73.00, but visibility
node visits rose from 44.12 to 46.34. The initial visibility-ordering hypothesis
therefore was not a uniform win: fewer callbacks and less ordinary-ray work
accompany the measured overall improvement. These counts do not assign exact
milliseconds to its causes. Samples remained exactly two and average bounce
iterations remained 2.912 per path; separate live scenes are not bit-identical.

Local evidence under `build-widescreen/software-lighting-audit/`:
A1 `run-7c3b1155a58c45dbbcbacaf25d808856`,
B1 `run-984ee7278ca7434c9a61e9c8f179ee21`,
A2 `run-ed9841c4f16249e38f82ea9e85cd2d7d`,
B2 `run-d63738034ce842fca055bfd6817e7d6a`.
Counters/validation `run-9e39716874e2472d92717d2fa1ff1d0d`, compared with
pre-change counters `run-42823b01fbe6417fbf2b2f25bb2bfaf1`.
The original source/payloads/renderer are retained under
`build-widescreen/software-order-audit/before-c608c45ca3c94fa29e69de7edb7b68f7`.

## Checked on 2026-09-12

- The full-lighting/guide payload and reference-query checks passed on NVIDIA
  GeForce RTX 5070 and AMD Radeon integrated graphics (16,384 resumable queries
  per device, plus the 4,096-ray BVH builder/traversal regression fixture).
- Windowed Q3DM6 software-to-hardware and Q3DM0 hardware-to-software tests
  completed normally in about 38 seconds each. They used isolated 640x360,
  2-sample/2-bounce settings with NVIDIA runtime features disabled. Both
  synchronization-validation logs were empty. These are functional tests only;
  full-game performance on older/non-RT GPUs remains unmeasured.
- All 29 pre-existing hardware path-tracer shader payloads were unchanged.
  Recompiled hardware integrator/guide shaders matched their original bytes;
  the hardware scene-dispatch function was also verified unchanged.
- An earlier three-restart test exceeded the 45-second guard while recompiling
  its final pipeline; it was not counted as a pass. Separate two-mode tests
  subsequently covered both switching directions within the guard.
