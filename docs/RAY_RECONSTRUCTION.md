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

With `r_pathTracingScale 1`, 1080p DLAA activation should report `1920x1080 -> 1920x1080`, not Quality's
lower input resolution. RR also supports the available DLSS upscaling modes.
`pt_info` must report successful RR evaluation, not merely a requested cvar.

RR defaults **on**, but requires selected/supported Vulkan path tracing and a
DLSS/DLAA mode. The [1.0 preset](RELEASE_DEFAULTS.md) selects PT, DLSS Quality,
half-scale tracing, three requested samples, adaptive sampling on and four
bounces. An existing saved `r_dlssRayReconstruction 0` remains an opt-out;
all explicit saved choices remain in force.

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

## RR-only debug view

With Vulkan path tracing and RR active, use:

```text
/r_pathTracingRRDebug 1
```

Use `/r_pathTracingRRDebug 0` to restore normal presentation, or
`/toggle r_pathTracingRRDebug` to switch back and forth. This is a live,
non-archived console control; no restart or cheat mode is required.

The view displays the **current successful RR result**, with only the existing
exposure, tone mapping and gamma conversion needed to show HDR on the SDR
swapchain. It is not a raw/unclipped HDR export. Sharpening and the adaptive
budget overlay are disabled for the display conversion; bloom, night vision,
all local post-effect passes and HUD/UI-model drawing are bypassed. The traced
world, including the traced weapon when enabled, is unchanged. No extra RR
evaluation, image allocation or full-frame copy is introduced. It does not
change the sample budget, render scale, integrator or saved effect settings.

Frame Generation is suspended for the diagnostic view through the runtime
activation switch, without changing its saved toggle/multiplier. Console, chat
and menus temporarily suspend the clean view so their controls remain visible;
closing them resumes it. The engine's read-only `r_debugUIActive` signal handles
this independently of mod VMs. The diagnostic cvar does not belong to the
shared rendering profile.

If no world is being drawn, mode 2 is not selected, or RR is unavailable/fails,
normal presentation is retained—native/SR fallback is never labeled RR output.
Console messages report active/suspended/unavailable transitions. The read-only
`r_pathTracingRRDebugActive` reports actual clean-view presentation for the
current frame; it is naturally zero while the console is open.

## Sampling and scheduling

| Control | Default | Meaning |
| --- | ---: | --- |
| `r_dlssRayReconstruction` | 1 | Request RR; restart to change |
| `r_pathTracingSamples` | 3 | Fixed samples per pixel unless adaptive is enabled |
| `r_pathTracingAdaptive` | 1 | Optional half-budget sampling on eligible history, including 2 → 1 |
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

The RR-only debug view passed the 2026-09-13 routing/recovery scenario on the
final builds: `run-9a1e74eda6594deba3addb5f80c39cfd` (DLAA/RR PT, normal exit in
6,438 ms, validation off) and `run-26b9d00cccdb4441b3d383b5646fbd06` (SDK-free
raster, normal exit in 3,531 ms, validation log contained only instance begin/end
markers). Inspected captures and `pt_rr_debug_check.py` confirmed the chart/HUD
were bypassed only in the RR view, forced later effects did not replace it,
normal presentation returned on toggle-off, and console/options/disconnect
remained usable. Both used isolated settings with unchanged saved-settings
hashes and FG off. This is not a performance measurement, PT validation pass,
injected RR-failure test or FG-on transition verification. An earlier raster
capture caught the tail of the console closing animation; the final fixture
uses an immediate console slide to keep the routing check deterministic.

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
