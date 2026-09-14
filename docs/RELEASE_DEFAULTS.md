# VQ3 Evolution 1.0 release defaults

The maintainer's saved preferences from 2026-09-14 are built into
[`release_defaults.h`](../code/qcommon/release_defaults.h). The only requested
changes to that snapshot are **3 ray samples, 4 bounces and 2× Frame Generation**.
This includes weapon lights, exposure, all post-effect parameters, effect order,
enabled states, UI/HUD, audio and gameplay preferences. It excludes identities,
bindings, passwords, paths, device names, cached servers and obsolete experiments.
No personal configuration or game content is included in the package.

## Main preset

| Setting | Release value |
| --- | --- |
| Renderer | Vulkan hardware path tracing |
| Window | 1920×1080, windowed; VSync off |
| Tracing | Half width/height; 3 requested samples; 4 bounces |
| Adaptive sampling | On, as saved; eligible RR pixels can use fewer samples |
| NVIDIA reconstruction | DLSS Quality selected, Ray Reconstruction on |
| Frame Generation / Reflex | On, 2× target / On + Boost, when supported |
| DLSS sharpening | 0.8 |
| PT exposure | 16; auto on; speed 0.25; target 0.1766; limits 0.01–65.5 |
| Ambient / sun / local-light radius | 0 / 1 degree diameter / 3 world units |
| Filtering | 16× anisotropic, full texture detail |
| UI / HUD scale | 50% / 100% |
| Rendered-frame cap | 85; generated frames are separate |
| Neural Rendering | Off (WIP); its saved tuning remains in the preset |

The PT render-scale override takes precedence over the selected DLSS input
ratio. At 1080p the intended RR input is 960×540. This is not native-resolution
DLAA. Set `r_pathTracingAdaptive 0` for a flat three-sample budget.
Unsupported hardware features retain the existing fallback paths; a requested
setting is not proof that a runtime actually activated.

| Weapon setting | Release value |
| --- | ---: |
| `r_muzzleFlashBrightness` | 0.01 |
| `r_muzzleFlashLightScale` | 0.005 |
| `r_rocketBrightness` | 1 |
| `r_rocketLightScale` | 0.02 |
| `r_rocketExplosionLightScale` | 0.03 |
| `r_lightningGunLightScale` | 0.5 |

## Post effects

Master on. Ascending order: chromatic (0), animated film grain (2), lens dirt (3),
lens distortion (4), tone controls (5), vignette (6), color grade (7), DOF (8,
disabled), motion blur (9, disabled), bloom (10). Gaps in the ordering are valid.
Every supplied effect parameter, including disabled-effect tuning, is recorded
in the header's `r_fx_` entries. These are the authoritative 1.0 values, not the
neutral/example fallback numbers in standalone `.effect` files.

Shift+F10 exposes the same defaults. Custom effects not in the release preset
start disabled and use their own manifest defaults. Existing saved settings
win, including legacy bloom preferences imported on first migration.

## Implementation and checks

Archived cvar registration supplies the preset as the factory/reset value.
Existing configs and explicit console/command-line settings remain untouched.
The same registration covers native and QVM game modules and mod UI controls;
dedicated servers retain their original defaults. Read-only capabilities,
temporary/debug cvars, explicit user-created sets and server state are excluded.

Run `python tests/release_defaults_check.py` for all 344 entries, shader bounds
and the real cvar implementation's reset/config/latch precedence tests (normal,
fast-math and dedicated builds). `run-engine-options-check.ps1` checks panel
defaults and edits. `run-release-smoke.ps1 -OfficialDefaults -Mode pt` starts
with an empty isolated profile and no graphics-quality overrides, captures the
Q3DM0 pillars, and writes its resulting config for comparison. These are
functional tests, not FPS benchmarks.

Mode 1 remains WIP. The saved optional software-denoiser request is retained,
but its experimental adapter is excluded from this package; native filtering
is the fallback. Existing settings are not forcibly reset by installing 1.0.
