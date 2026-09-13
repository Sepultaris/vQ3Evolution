# RR shader capture — 2026-09-09

Historical capture of the 2026-09-09 Ray Reconstruction (RR) renderer, after
repairing Nsight Graphics attachment. It was inspected with source-line samples. This is a
profiling result, **not a renderer optimization or a measured FPS improvement**.
It is not a fresh profile of subsequent builds; see [current status](../STATUS.md).

## Attachment fault and repair

Windows excluded TCP range 49152–49251 covered the complete default Nsight
connection range, 49152–49215. Loopback bind attempts returned `AccessDenied`.
The injected game had no target listener; its main thread waited inside
`WarpVizTarget` at the first `vkQueueSubmit` during depth-attachment setup.
A noninvasive stack inspection and the queue-submit call site confirmed this;
it was not evidence of an expensive rendering frame.

Moved Nsight to the available range **55000–55063** in both settings stores:

- GUI: Tools > Options > Connection (`ConnectionBasePort=55000`, 64 ports),
  stored in `%APPDATA%/NVIDIA Corporation/NVIDIA Nsight Graphics.ini`.
- CLI: DWORD values `ConnectionBasePort=55000` and `ConnectionMaxPorts=64`
  under `HKCU/Software/NVIDIA Corporation/NVIDIA Nsight Graphics/Connection`.

Changing the GUI alone did not change the CLI: its next log still searched
49152–49215. After correcting the CLI store, the same capture path connected
on 55000–55063 and reached presenting in 4.06 seconds.
No firewall, Windows exclusions, driver, service or counter-permission changes
were required. The original GUI configuration is backed up in the ignored
`build-widescreen/nsight-rr-20260909/nsight-settings-before-port-fix.ini`.

`tests/run-pt-nsight.ps1` now checks the CLI range before launching the profiler.
It also explicitly forces Frame Generation off on the game command line so
shared archived settings cannot undo the isolated benchmark override.
The offline launcher check passes occupied-port rejection, successful probing,
listener release, quoting, late/missing exit evidence and owned-child cleanup.
The real host check accepts 55000 and rejects the old range with `AccessDenied`.

## Accepted capture

Nsight Graphics 2026.3.1, RTX 5070/GB205, GPU Trace Profiler with real-time shader
profiling and unaltered GPU clocks. Isolated q3dm6 fixed-camera scenario:
1920×1080 DLAA, RR on, fixed two samples, six bounces, adaptive sampling off,
Frame Generation off, Neural Rendering off, dynamic-opaque experiment off.
Exposure remains 5 and anisotropic filtering 16. Runtime logging confirms
native-resolution RR, FG off and the 8×128 RR tracing workgroup.

The trace starts after 180 presented frames, requesting 250 ms; the loaded
report covers 242.42 ms. The profiler and game exited successfully after
**28.281 seconds**, with no timeout. The temporary diagnostic runner had a
90-second safety guard; that was an agent-selected safeguard, not a user-imposed
test deadline. No additional game runs were needed to inspect the saved trace.

Evidence directory (ignored by Git):
`build-widescreen/rt-audit/performance-nsight-rr-current-20260909-f/nsight/`

- `vQ3Evolution_2026_09_09_13_43_11.ngfx-gputrace`: 391,394,116 bytes;
  SHA-256 `931b4461d7ba7b47ea25f11f25040a5e1492796582579b9cd4925d5427d97fa1`.
- `guard.log` and `result.json`: connection, capture and shutdown evidence.
- `shader-pipelines.csv` and `source-hotspots.csv`: exports from the loaded report.
- `verified-analysis.json`: validated sample counts, source contents and hashes.
- Runtime verification: `../baseq3/qconsole.log`.

The final log's connection-error message occurs after intentional target
termination; the saved native report opens correctly and the process exits 0.
Optional CLI auto-export remains disabled because it previously crashed after
saving a report. Exporting these tables through the GUI succeeded.

## Measured shader activity

55,985,320 warp-state samples. Percentages below are **sample shares, not
exclusive GPU time, frame-time shares or predicted FPS gains**.

| Shader/group | Share of all samples | Long Scoreboard share within that shader/group |
| --- | ---: | ---: |
| `pt_rr_trace.comp` | 76.00% | 61.84% |
| NVIDIA SDK group | 18.42% | 24.94% |
| `pt_rr_guides.comp` | 4.24% | 50.15% |

The main tracing shader uses 64 registers per thread, 48 KiB shared memory,
and reports a theoretical maximum of 32 resident warps. Guides use 168 registers,
8 KiB shared memory and a maximum of 12 warps. These are current RR shaders,
not the historical pre-RR shader with 255 registers.

Source hotspots in the main tracer (`0xdd016ea6dbb19ccd`):

| Location | Observed source | Share of all samples | Long Scoreboard within that location |
| --- | --- | ---: | ---: |
| `pt_integrator.glsl:562` | `visibility()` ray-query advance loop | 7.69% | 75.02% |
| `pt_integrator.glsl:380` | `trace()` surface-hit ray-query advance loop | 4.51% | 61.78% |
| `pt_integrator.glsl:656` | Emitter cumulative-power lookup in binary search | 1.97% | 94.10% |
| `pt_sampling.glsl:32` | Alias-table probability comparison | 1.60% | 92.70% |

Selecting the visibility hotspot in Nsight navigates to the source-correlated
`OpRayQueryProceedKHR`. Exported source statements were compared with the actual
GLSL files. Nsight's malformed middle CSV headings are not trusted: the shader
analysis reconstructs shares from the 17 stall-count columns and checks every
row against its exported sample share; source analysis uses the verified prefix.

Long Scoreboard means waiting on a memory dependency; it does **not** establish
memory-bandwidth saturation. The query loops include driver-generated traversal,
and a sampled consumer instruction does not by itself identify the producer
load to optimize. The `main()` entry line also has 5.47% attribution; that coarse
compiler mapping is not evidence that the function declaration is costly.
See [NVIDIA's shader profiler guide](https://docs.nvidia.com/nsight-graphics/UserGuide/shader-profiler.html)
for dependency attribution and warp-state definitions.

## Dependency follow-through (offline + report UI)

The selected `pt_integrator.glsl:562` row was opened in the saved Nsight report
and its RTCORE instruction group was expanded. The child instruction is
`comp.10000.spv ...:15281` (100% of the 16-instruction RTCORE group). The
source/IL mapping at that location is:

```text
pt_integrator.glsl:559  OpLoad sceneAS
pt_integrator.glsl:560  OpRayQueryInitializeKHR
pt_integrator.glsl:562  OpRayQueryProceedKHR
```

This is acceleration-structure traversal work. There is no ordinary shader
global-memory load between initialization and the `Proceed` instruction, so
rewriting a supposed pre-query buffer load would not address this stall. The
surface-hit loop at `pt_integrator.glsl:380` has the same dependency shape:
`sceneAS` load, `OpRayQueryInitializeKHR`, then `OpRayQueryProceedKHR`. The
material and triangle-material loads occur after a candidate hit and are not
the producer of the sampled `Proceed` instruction.

The two light-selection hotspots have concrete upstream loads instead:

| Consumer source line | Producer and resource | Dependency shape |
| --- | --- | --- |
| `pt_integrator.glsl:656` | `OpLoad float` of `emitters[mid].cumulativePower`; `Lights` SSBO binding 10, member `emitters` | A dynamically indexed load feeds the compare that chooses the next binary-search midpoint, so each iteration depends on the previous load/result. |
| `pt_sampling.glsl:32` | `OpLoad vec4` of `lightAliases[index]`; `LightGrid` SSBO binding 34, member `lightAliases`, at source line 31 | The line-32 `fract(column) < entry.x` comparison waits on the alias-table vector load and its dynamic index. |

`pt_integrator.glsl:672` similarly reads `Lights.pointCones[light].w`
(binding 10, member `pointCones`) before the cone branch; it is a smaller
hotspot (1.42% of all samples) but has the same dynamic-load pattern.
`pt_integrator.glsl:195` is in the texture-cache path: the source-correlated
instruction performs a binding-9 texture lookup and filtered sample. The
current report does not establish whether that sample is latency- or
bandwidth-limited, so it is recorded as a target rather than a diagnosis.

These mappings identify where the dependency chains enter the shader, but they
do not prove that changing them will improve frame rate. The evidence supports
two different investigation tracks: reduce ray-query/traversal cost (query
count, acceleration-structure quality or ray setup) for the RTCORE rows, and
measure/cache or reorganize the serial dynamic light-table accesses for the
light-selection rows. No renderer source was changed in this pass and no FPS
improvement is claimed. A safe next experiment must isolate one of those paths
and compare the same scene, resolution, samples, bounces and RR settings.

## Alias-PDF compatibility fix (follow-up)

The first runtime check of `r_pathTracingAliasPDF 1` dropped from roughly
35 FPS to 20 FPS. This was a pipeline-selection defect, not a measured cost of
the alias-PDF load: lighting mode 61 (mode 57 plus the alias-PDF bit) was
falling out of the compact-transport selector, and the RR sampling gate also
accepted only mode 57. The renderer therefore used the non-compact integrator
and did not run the dedicated RR tracing dispatch.

The selector now keeps separate compact pipelines for alias-PDF off/on, passes
the specialization through to the compact shader, and keeps separate RR guide
and trace pipelines for both variants. The alias-PDF setting can therefore be
tested without silently disabling compact transport or RR. This fix has passed
the GPU-free compact-pipeline, alias-table, and native lifecycle fixtures. It
has not yet had a GPU A/B measurement, so it makes the comparison valid but
does not claim a performance gain.

## Build provenance and limits

The installed pre-SER renderer was not replaced:
SHA-256 `D8ACB5813B4B2A604AF28A7DE5E70F0F38713939648BAC92922136756710422E`.
Saved and shared settings hashes are unchanged in `result.json`.

The isolated symbol renderer is
`build-widescreen/nsight-rr-20260909/renderer_vulkan_x86_64.dll`, SHA-256
`0C80D723623FB013C6472664359AC5479190D13C9990D1849F23594073FEDC67`.
It uses the existing dynamic-opaque candidate CPU objects with that experiment
disabled; it is **not byte-identical** to the installed renderer. No SER path
was reintroduced. A stale saved SER cvar is not evidence of an active SER pipeline.

The optimized RR trace, guides and post payloads include glslang source/line
metadata. Normalizing production and symbol payloads with
`spirv-opt --strip-debug --compact-ids` gives matching SHA-256 hashes:

| Payload | Matching normalized hash |
| --- | --- |
| RR trace | `f807bad9618296ff8a45002618bf67ae1f9243b97d1ca46535e4570797d08de9` |
| RR guides | `0a9441cbba4ff9d7116afcd01143d6bdb7c34aa42bcf0c95a64af6450c40e554` |
| RR post | `acbe1828eb64e0308e8341c9a94b4ef3b3c4479e339486ea2404c63cf9b1436f` |

Source-line correlation works. Full NonSemantic function/call-site debug
metadata is absent, so Nsight's missing-function-info warning and `Unknown`
function names remain; a nested-function flame graph is not available.
One short, fixed-scene profile is not evidence of performance across every map.

The attachment blocker and missing current source-line evidence are resolved.
The capture supports investigating memory dependencies in the query and light
selection paths. It does not yet prove that a particular code change will help;
any such change still requires a same-quality, unprofiled before/after comparison.
