# DLSS Ray Reconstruction

The Windows x64 Vulkan renderer integrates RR through NVIDIA Streamline 2.12.0's
public interface. It uses `sl.dlss_d.dll` and `nvngx_dlssd.dll` supplied by the
official SDK fetch/build workflow. No Neural Rendering DLL, replacement PK3 or
runtime shader override is required. See [NVIDIA setup](DLSS.md) for installation.

## Enable at native resolution

In **Shift+F10 → Lighting**, select Path tracing; in **NVIDIA**, select DLAA and
Ray Reconstruction, then Apply. Console equivalents:

```text
/r_rayTracing 2
/r_dlss 5
/r_dlssRayReconstruction 1
/vid_restart
/pt_info
```

For 1080p DLAA, activation should report `1920x1080 -> 1920x1080`, not Quality's
lower input resolution. RR also supports the available DLSS upscaling modes.
`pt_info` must report successful RR evaluation, not merely a requested cvar.

RR defaults **on**, but requires selected/supported Vulkan path tracing and a
DLSS/DLAA mode. It does not automatically enable those prerequisites. An existing
saved `r_dlssRayReconstruction 0` remains an opt-out. Fixed sampling defaults to
two samples with adaptive sampling off. Bounce count is independent (source
default four); all explicit saved choices remain in force.

## Rendering contract

1. A guide pass exports physical depth and motion, linear diffuse/specular
   albedo, world-space unit normals, linear roughness and specular hit distance.
2. The RR-specific combined-radiance tracer writes fresh noisy HDR directly.
   It preserves the chosen sample/bounce budget and material transport. It does
   not feed native filtered history, exposure, gamma or tone mapping into RR.
3. NVIDIA RR reconstructs the HDR image. Native temporal/spatial filtering,
   ordinary DLSS color evaluation and experimental NR are bypassed for that frame.
4. Exposure, tone mapping and native post-DLSS sharpening run on the result;
   HUD/menu rendering remains at output resolution afterward.

Camera cuts, renderer restarts and diagnostic/native/RR transitions invalidate
history. Missing support or allocation failure retains the native fallback.
Evaluation failure disables RR for the session, tone-maps fresh HDR for the
current fallback frame, then resumes native reconstruction with reset history.
That transition frame can be noisy. Failures are reported rather than treated
as successful RR activation.

The seven RR images add approximately 95 MiB at native 1080p, excluding NVIDIA's
internal allocations. Native fallback buffers are retained. RR sample-reuse
history aliases compatible native scratch/history storage rather than adding
another full set of images. This is not a claim that total VRAM use is 95 MiB.

## Sampling and scheduling

| Control | Default | Meaning |
| --- | ---: | --- |
| `r_dlssRayReconstruction` | 1 | Request RR; restart to change |
| `r_pathTracingSamples` | 2 | Fixed samples per pixel unless adaptive is enabled |
| `r_pathTracingAdaptive` | 0 | Optional half-budget sampling on eligible history, including 2 → 1 |
| `r_pathTracingLightReuse` | 1 | Temporal/spatial sample reuse for primary-surface map lights |
| `r_pathTracingAdaptiveDebug` | 0 | Budget overlay; not a quality mode |
| `r_pathTracingRRRows` | 0 | Cheat-protected, non-archived test override; restart to apply |

Adaptive sampling and light reuse apply to the supported compact RR route, not
the frozen reference or experimental staged pipeline. Geometry, variance,
identity and motion checks reject unsafe adaptive reuse. Keep adaptive off for
a flat two-sample budget. Light reuse does not add reservoirs for every light
type or make this a complete RTXDI integration.

The automatic RR workgroup remains eight columns by 128 rows on supported
NVIDIA hardware, with smaller hardware-limited choices elsewhere. Explicit
row requests are 8, 32, 64 or 128; invalid/unsupported requests use automatic
selection. The latest smaller-group comparison was slower and did not change
the default. Different group shapes have not been implemented or measured.

## Limitations

- Fine textures can soften or trail during motion at low sample counts.
- Rough reflection hit distance uses a perfect-reflection query approximation;
  dedicated specular object motion vectors are not yet supplied.
- Dielectric guides describe the first interface, not an entire nested optical
  path. Separate transparency guides remain unimplemented.
- Particles, complex glass/water motion and arbitrary mod materials need broader
  scene testing; RR does not repair an incorrect material conversion.
- NVIDIA shutdown, FG-on/restart behavior and separate menu validation failures
  remain open. See [current status](STATUS.md).

## Evidence and tests

The [RR development record](archive/RAY_RECONSTRUCTION_DEVELOPMENT.md) preserves
input-contract checks, jitter regression, sampling math, GPU fixtures and dated
performance comparisons. The latest combined-radiance and workgroup timings
are **provisional rendering-phase observations** because the processes reached
their shutdown deadline. Do not compare runs with different bounce counts or
present generated frames as rendered-FPS gains.

Use [testing instructions](TESTING.md) for offline resource-failure, transport,
workgroup and shader-payload checks. GPU fixtures and game captures are explicit
opt-ins; compilation alone is not a visual or performance pass.

Primary reference: [NVIDIA Streamline DLSS-RR programming guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.12.0/docs/ProgrammingGuideDLSS_RR.md).
