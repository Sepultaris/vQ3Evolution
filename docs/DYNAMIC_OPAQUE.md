# Dynamic opaque-geometry experiment

This candidate separates ordinary solid moving geometry from callback-required
geometry, without changing shaders, resolution, samples, bounces or materials.
It is **disabled by default**. The matched test below found no median FPS gain.

`r_pathTracingDynamicOpaque 1` enables it in a development map; `0` disables it
without restarting. The switch is cheat-protected and not archived. The first
frame of each mode logs opaque regular/weapon and callback triangle counts.

Opaque regular and weapon geometry get separate BLAS instances. Their custom
indices preserve the existing shader's primitive addressing. Cutouts, glass,
filters, additive surfaces, sky and decals retain callbacks. Regular surfaces
with first-view exclusion or no-shadow flags also retain callbacks. Weapon
instances remain visible only to first-person queries. The disabled path keeps
the original two partition scans and three nonempty instance categories.

## Build and checks

A separate Windows x64 Release renderer with NVIDIA support was built at
`build-widescreen/opaque-audit/renderer_vulkan_x86_64.dll`. The normal installed
renderer remains the verified pre-SER backup; it has not been overwritten.

```text
make PLATFORM=mingw64 ARCH=x86_64 BUILD_DIR=build-widescreen BR=build-widescreen/opaque-audit TARGETS=build-widescreen/opaque-audit/renderer_vulkan_x86_64.dll USE_NVIDIA_DLSS=1 BUILD_SERVER=0 -j4 release
python tests/pt_dynamic_opaque_check.py --cc gcc
python tests/pt_material_layers_check.py --cc gcc --cxx g++ --sdk <VulkanSDK>
```

The partition fixture compiles the actual production functions at O1 and O3,
checking visibility flags, material exclusions, empty ranges, stable ordering,
indices/material correspondence and disabled behavior. Existing material
conversion/composition checks also pass. These do not replace a GPU visual test.

## Runtime evidence: 2026-09-09

`performance-dynamic-opaque-20260909-a` loaded the separate candidate DLL with
1920x1080 DLAA/RR, fixed two samples, saved six bounces, FG/NR/adaptive off.
The validation layer loaded with an empty validation log, but only the initial
disabled phase ran before the 45-second guard expired. This does **not** validate
the enabled path or establish a speedup. Saved configuration hashes were unchanged.
After the owned test closed, device-wide GPU utilization was still 54% and
Valheim was running. The run is excluded from performance conclusions.

`tests/pt_dynamic_opaque.cfg` supplies an off/on/off comparison at one settled
camera. `tests/run-pt-performance.ps1 -BuildDirectory build-widescreen/opaque-audit`
can run the candidate with the normal installation's assets and isolated settings.
Before promoting it, complete a matched comparison without competing GPU work,
verify actual opaque counts, and inspect transparency, weapons and shadows.

### Uncontended comparison: no median FPS gain

Runs `dynamic-opaque-old-20260909-b` and `dynamic-opaque-new-20260909-b`
tested the actual pre-SER DLL and separate candidate. Before testing, the GPU
reported 0% utilization and no game was running. Both used q3dm6, identical
settings, 1920x1080 DLAA/RR, fixed two samples, six bounces, FG/NR/adaptive off,
and no validation instrumentation. Executable and source/shared settings hashes
matched. Only the renderer binary differed.

Both completed all three 96-frame phases and screenshots. Camera coordinates
matched at all four checkpoints: (-1361,200,77), yaw 0. Each phase produced 47
timing records; trimming three at each edge leaves 41. The old DLL remained
baseline throughout; the candidate switched off/on/off. Pooled medians:

| Renderer | Records | Frame | Trace | AS build | Guides |
| --- | ---: | ---: | ---: | ---: | ---: |
| Pre-SER DLL | 123 | 28 ms | 20.207 ms | 0.416 ms | 1.345 ms |
| Candidate, off | 82 | 28 ms | 20.193 ms | 0.418 ms | 1.345 ms |
| Candidate, on | 41 | 28 ms | 19.904 ms | 0.595 ms | 1.549 ms |

The enabled phase logged 2,281 regular opaque triangles, 243 weapon opaque
triangles and 2,428 callback triangles, confirming activation. Compared with
the same-binary off control, tracing saved about 0.29 ms while AS build plus
guides cost about 0.38 ms more. Median frame time stayed 28 ms (35.7 rendered
FPS). The off control also closely matched the pre-SER DLL. Do not promote
this candidate as a performance improvement.

Fixed-view screenshots showed no obvious newly missing surfaces. Full-frame
mean absolute channel difference was 0.092/255 for old versus candidate off,
and 1.269/255 for candidate off versus on. Animation, noise and a fading pickup
notification contribute; this is not image equivalence or whole-game validation.

Rendering completed before the known NVIDIA shutdown wait (45.6 / 43.9 seconds).
The guards closed both processes at 90 seconds. These are completed
rendering-phase measurements, not clean lifecycle/exit passes. The runner and
guard now accept explicit limits up to 120 seconds; timeouts remain safety
stops, never acceptance criteria.

The installed pre-SER renderer and saved settings remain unchanged. The
candidate remains disabled by default; no renderer-source changes or deployment
were made in response to these timings.
