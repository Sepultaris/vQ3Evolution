# GPU hardware profiling

This workflow diagnoses the normal optimized renderer. It is not a performance
optimization and profiler frame times must not be used as FPS acceptance results.

The original pre-RR captures were analyzed in
[GPU performance diagnosis](archive/GPU_PERFORMANCE_ANALYSIS.md). Those counters are
historical, not a profile of the current combined RR tracer. See
[current status](STATUS.md) and the [later RR record](archive/RAY_RECONSTRUCTION_DEVELOPMENT.md)
before selecting a new capture target.

The dated RR capture and source-line findings are recorded in
[the 2026-09-09 profiler repair and capture](archive/GPU_PROFILE_20260909.md).
They describe that build and configuration, not a fresh profile of later changes.

## Tool versions used in the 2026-09-08 setup

The setup machine has Nsight Systems 2025.6.3 and Nsight Graphics 2026.3.1.
The latter was installed from NVIDIA's signed Windows installer (verified NVIDIA
Corporation Authenticode signature), without changing the display driver or
rebooting. The RTX 5070/GB205 supports the selected Blackwell counter sets.

Use standard Windows administrator approval for a capture. Do not weaken global
GPU-counter permissions, change driver settings, disable NVIDIA services or leave
an elevated background listener. Run GPU captures outside a filesystem sandbox:
restricted NVIDIA cache/service access previously caused misleading startup and
shutdown failures. CPU/API sampling is process-tree scoped; hardware counters
are necessarily device-wide and may include other applications' GPU work.

## Prepare the build

Build the release executable normally. Build the owned-process deadline helper
with a Windows GCC toolchain (outputs below are ignored by Git):

```powershell
New-Item -ItemType Directory -Force build-widescreen/rt-audit/tools
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tools/pt-bounded-process.c -o build-widescreen/rt-audit/tools/pt-profile-guard.exe
```

For source-line shader analysis, generate optimized shaders with debug metadata
and rebuild the release renderer:

```powershell
code/renderer_vulkan/shaders/compile-raytracing.ps1 -VulkanSDK $env:VULKAN_SDK -DebugSymbols -Shaders pt_rr_trace,pt_rr_guides,pt_rr_post
```

`-DebugSymbols` adds glslang `-g` source/line information, **not** an unoptimized
shader build. The existing `spirv-opt -O` pass and Vulkan validation remain.
`-OutputDirectory` can generate a separate normal/symbol payload set for offline
comparison. Normalize both using `spirv-opt --strip-debug --compact-ids` and
compare their SHA-256 hashes before accepting symbol payloads. The historical
setup performed this check on `pt_compact_transport`, `pt_guides`,
`pt_material_cache`, `pt_temporal` and `pt_denoise`. The current RR trace, guides
and post payloads passed a separate fresh comparison on 2026-09-09; hashes and
isolated-build provenance are in [that capture report](archive/GPU_PROFILE_20260909.md).
Choose the shaders actually active in the captured configuration and restore
normal payloads before a production commit.
These are source/line symbols, not NonSemantic function/call-site debug info.

The opt-in process environment variable `VQ3E_GPU_LABELS=1` names the normal
path-tracing, guide, material preprocessing, temporal and spatial pipelines,
and labels the seven coarse rendering stages. It inserts no waits, GPU queries
or barriers. Without the variable, no debug-utils calls are made. Missing
debug-utils support is harmless. Labels and cached names reset on renderer
shutdown. The capture runner enables labels only in its own process tree.

## Capture at saved settings

Run these in PowerShell 7, one capture at a time, with a unique label:

```powershell
tests/run-pt-nsight.ps1 -Tool Systems -Label systems-unique-label
tests/run-pt-nsight.ps1 -Tool Graphics -Label graphics-unique-label
```

The launcher prepares an isolated graphics-only configuration before elevation,
records configuration/build hashes, and returns while a bounded worker runs.
It refuses to run alongside an existing game or overwrite an old capture. Saved
windowed Vulkan path tracing is required; no silent quality/display overrides
are allowed apart from the explicit benchmark policy: **Frame Generation is
always forced off in the isolated profile**, regardless of the saved value.
The override is recorded in `settings.json` and never written to saved settings.
Quality comes from the selected saved profiles, not a fixed preset in this
guide. The 2026-09-08 setup used 1080p DLAA and four samples/four bounces; later
RR comparisons used two samples and different saved bounce counts. Inspect
`settings.json` and actual runtime state for every run. `run-pt-performance.ps1 -BenchmarkNeuralRendering 1`
explicitly measures NR overhead with FG still off; it records that override too.

`tests/pt_nsight.cfg` exercises a fixed camera followed by camera rotation.
It disables the existing timestamp/shader-clock instrumentation and the rejected
staged prototype. All saved quality settings, exposure and NR controls remain.

The game is launched suspended and assigned to a kill-on-close Windows job before
it runs. For Systems, the independent native helper owns the game and enforces
its 45-second deadline while report export may continue separately. For Graphics,
the helper owns the profiler and its game descendants, with a 45-second deadline
for the whole group. Killing/crashing the helper also kills its owned children.
It cannot attach to or kill a user-started game. Only the three standard I/O
handles are inherited; the job handle is never inherited. Export has a separate
120-second worker timeout, never an extension of the game's deadline.
Systems can finish export before the guard flushes its final exit record, so the
worker waits for that evidence separately. If Nsight's injection DLL then keeps
the completed helper alive, cleanup is limited to the logged owner PID after
checking its executable and unique capture command line, never a process-name
kill. The child game's exit and the helper's later DLL cleanup are distinct.
The final setup capture exposed an additional Nsight teardown defect: Windows
reported native exit code 0 for its original helper but retained an unsignaled
process/image mapping. The exact owned Nsight agent was terminated, without
touching NVIDIA services, but the old helper image remained locked. That original
`pt-bounded-process.exe` is no longer the installed launcher; the current build is
`pt-profile-guard.exe`. The cleanup result is preserved in `helper-cleanup.json`.
This residual tool/Windows cleanup issue is **not claimed fixed**; future capture
summaries flag incomplete cleanup instead of silently accepting it. No driver
reset, global counter-access change or reboot was performed.

Systems records ten seconds after an eight-second delay, with the GB20x top-level
counter set at 1 kHz. Graphics takes a 250-ms shader/counter sample after 120
presented frames, with an 8-MiB hardware-event buffer and a lower shader-sampling
frequency to bound transfer size. It does **not** lock GPU clocks. A fixed
wall-clock Graphics trigger was unreliable across cold/warm pipeline caches.

Nsight Graphics 2026.3.1 has a launch conflict: passing its documented
`--platform "Windows (x86_64)"` is consumed by Qt as a platform-plugin name and
causes an abort. Omit this redundant option and let the native platform default
apply. `QT_FORCE_STDERR_LOGGING=1` keeps such startup errors in the capture log.
Do not disable shader/pipeline collection to make a failed shader capture appear
successful.

On this host, `--auto-export` also crashed in Qt6Core **after** saving the native
trace, while reopening it for metrics export. The runner therefore preserves
the complete native shader trace without that optional export step. The verified
capture `performance-nsight-graphics-setup-20260908-g/nsight/` completed with exit
0 in 20.2 seconds, no timeout or buffer-drop warning, and saved a 414,695,848-byte
`.ngfx-gputrace` containing the VQ3E stage labels. The tool intentionally closes
the target once the report is saved, so the scenario's later quit marker is not
required for this capture. Automatic metrics export and detailed shader-source
inspection remain separate from successful data collection; do not claim the
shader-stall root cause has been established just because a report was saved.

### Windows reserved-port attachment failure (2026-09-09)

If Graphics repeats `Searching for attachable processes` and the game stops on
its first queue submission, inspect the connection before changing renderer code.
On this host Windows reserved TCP ports 49152–49251, covering Nsight's entire
default target range (49152–49215). A noninvasive stack inspection showed the
main thread waiting in `WarpVizTarget` during `vkQueueSubmit`; no target listener
existed. Binding the excluded ports failed with `AccessDenied`.

Read reservations using `netsh interface ipv4 show excludedportrange protocol=tcp`
(also check IPv6). Do not delete exclusions, disable networking/services or
disable the firewall. Choose an available range for the profiler instead.

Nsight Graphics 2026.3.1 stores these settings separately:

- GUI: **Tools > Options > Connection > Base Port**, saved in
  `%APPDATA%/NVIDIA Corporation/NVIDIA Nsight Graphics.ini`.
- CLI: `HKCU/Software/NVIDIA Corporation/NVIDIA Nsight Graphics/Connection`,
  DWORD values `ConnectionBasePort` and `ConnectionMaxPorts`.

Both now use base port **55000**, maximum ports **64**, on this host. The GUI INI
was backed up before editing. Changing the GUI alone did not affect the CLI;
after its native settings were corrected, the CLI attached and captured normally.
This is a per-user profiler setting, not a change to game settings or Windows
port reservations. Recheck availability if the machine's reservations change.

The Graphics runner now tests its configured range before launching the game
and records the chosen range. It fails immediately if no port can be bound.
The probe closes its socket immediately: it neither leaves a listener behind
nor guarantees availability against a later competing process. The capture log
must still show attachment and a successfully saved trace.

## Inspect and accept the evidence

Outputs are in `build-widescreen/rt-audit/performance-<label>/nsight/`:
`result.json`, `guard.log`, tool logs and the native capture. `settings.json` and
the isolated `baseq3/qconsole.log` are one directory above. A nonempty report
alone is not proof of useful hardware data. Reject timeouts, empty reports,
missing game workloads and buffer-overflow/drop warnings for shader attribution.

Export a Systems report with the installed `nsys.exe` (no game/GPU run needed):

```powershell
nsys export --type sqlite --output capture.sqlite capture.nsys-rep
python tests/pt_nsight_check.py capture.sqlite --require-labels
```

The checker verifies game-owned Vulkan API events, GPU workloads and actual
hardware counter samples. It preserves capture warnings and reports readable
stage ranges when present. Device-wide whole-capture averages mix workloads;
they do not by themselves diagnose a shader's bottleneck. Open native reports
in Nsight for timeline and shader-source inspection with the game closed.
In particular, annotation spans are not guaranteed execution-complete GPU stage
times: a near-zero annotation is not proof that reconstruction/post-processing
costs nothing. Verify actual shader workload timing before ranking those stages.

The initial Systems proof-of-access capture, before renderer labels were added,
is `performance-nsight-systems-setup-20260908-a/nsight/systems.nsys-rep`:
31,530 game Vulkan API events, 1,962 GPU workload records, and 2,824,015 readings
at 10,195 counter sample instants. Saved settings were unchanged. Its wrapper
process naturally has no Vulkan/NVTX events; the report also warns that some
game Vulkan events may be incomplete. Keep that caveat with any analysis.

The final labeled Systems capture is
`performance-nsight-systems-labels-20260908-a/nsight/systems.nsys-rep`:
23,110 game Vulkan API events, 2,212 GPU workload records, 2,838,419 hardware
readings, and all seven stage labels (47 path-tracing ranges). The game owner
reported exit 0 after 34.078 seconds, no timeout, and saved settings were unchanged.
Its original `result.json` caught the late guard-log flush described above and is
retained as failed-summary evidence; offline report/guard verification succeeded,
and the new runner waits for the exit record. No shader-stall diagnosis or new
performance gain is claimed by this setup pass.

## Offline regression checks

`pt_gpu_labels_check.c` executes the actual annotation helper with mocked Vulkan
entrypoints: disabled/missing extension, balanced/skipped ranges, another command
buffer, once-only pipeline naming and reset. `pt_bounded_process_check.c` tests
the real native deadline, normal exit, invalid deadline and failed launch.
After compiling both native helpers, `pt_nsight_launcher_check.ps1` checks Windows
quoting and forcibly terminates its own helper to verify child cleanup. These
tests never open a game or use the GPU. The existing 213 renderer unit tests
also passed after the annotation/source-metadata changes.

References: [NVIDIA application profiling setup](https://docs.nvidia.com/nsight-graphics/UserGuide/configure-application.html),
[Nsight Systems guide](https://docs.nvidia.com/nsight-systems/UserGuide/),
[NVIDIA counter permissions](https://developer.nvidia.com/nvidia-development-tools-solutions-err_nvgpuctrperm-permission-issue-performance-counters).
