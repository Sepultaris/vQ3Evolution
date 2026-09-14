# Night-vision goggles

The Vulkan NV post effect uses a muted white-phosphor appearance inspired by
[this panoramic-goggle reference](https://imgur.com/y6TKkfc): soft overlapping
lenses, subdued side seams, fine animated grain and gentle highlight diffusion.
Green phosphor remains available. The image is not repeated or warped per lens;
the goggles process one already-rendered scene, before the HUD is drawn.

Console controls (live, without restarting the renderer):

| Command | Effect |
| --- | --- |
| `r_nvTint 1` | Cool white phosphor, matching the reference; default for new settings |
| `r_nvTint 0` | Green phosphor |
| `r_nvBrightness 1.6` | Default signal gain; range 0–8 |
| `r_nvGrain 0.4` | Default fine grain; 0 disables it, 1 is strongest |
| `r_nvVignette 1` | Full goggle mask; 0 gives an unobstructed full-screen effect |
| `r_nvNightVision 1` | Enable automatic activation by supported Urban Terror overlays |
| `r_nvOverride 1` | Force NV on for testing; use 0 to return to mod-controlled activation |
| `r_nvDebug 1` | Monochrome signal diagnostic without grain, tint, diffusion or mask; normal is 0 |

Existing saved preferences take precedence over defaults. Use `r_nvTint 1` if
an existing config still selects green. Tint, brightness, grain and vignette are
archived; override and debug are temporary. Other graphics settings are unchanged.

This is a visual approximation using the rendered colour image, not a thermal
sensor or a reconstruction of invisible infrared data. It cannot reveal detail
that was never rendered. All RGB channels contribute positively, gain rolls off
smoothly instead of clipping early, and the grain has zero mean. Very wide
screens keep a centred, connected mask instead of stretching the tubes apart.

The effect uses one compute pass with nine source samples per output pixel while
active, with no new image allocation or path-tracing rays. This is more sampling
work than the former one-tap effect; no frame-rate improvement is claimed.

## Scope and checks

Urban Terror activates the effect through its normal equipped-goggle item-use
command (`ut_itemuse`); `r_nvOverride` is not required. Its 2D mask and 3D
brightness surfaces are detected during front-end submission, before any HUD
command can finish the scene and run post-processing. Detection covers the
3.0–3.7 `nvgScope`, `nvgScope2`, `nvgStatic`, `nvgBright`, `nvgBrightA/B` and
older `gfx/items/` names, but not inventory icons or player goggle models.
The old colour/filter layers, including `nvgStatic`, are suppressed only while
the replacement is enabled and available. Mod colour presets no longer add
their old filter on top of the selected `r_nvTint` palette.

The post-pass requires active Vulkan temporal targets (path tracing or a
supported NVIDIA processing path). It now samples a lower-resolution scene
at normalized coordinates and writes the complete display-resolution NV image,
so native half-resolution tracing no longer needs DLAA to use the effect.
When the post-pass is unavailable, or automatic replacement is disabled, the
legacy mod overlays are retained. Raster-only rendering without temporal targets
therefore keeps the original goggles rather than losing their effect.
When Frame Generation is enabled, its HUD-less input is routed to the processed
NV image while goggles are active instead of the unfiltered scene.

`tests/nv_look_fixture.cpp` executes the production GLSL helper functions as C++:
RGB visibility, monotonic gain, highlight headroom, tint, mask symmetry and
connectivity across portrait through 32:9, and deterministic zero-mean grain.
Compile with `-I code/renderer_vulkan/shaders`. `post_NV.comp` is compiled,
optimized and SPIR-V validated by `compile-raytracing.ps1 -Shaders post_NV`.
`tests/rt_nv_look.cfg` captures off, white, green, unmasked and diagnostic states
in an isolated game run; activation and screenshot existence are checked by
`run-software-lighting.ps1`. Inspect captures separately for appearance.
`tests/nv_activation_check.py` executes the actual 2D/3D frontend submission
functions, checking early detection, next-frame reset, dropped commands and
disabled/unavailable fallback. `-Game Q3UT3 -Scenario rt_urban_nv.cfg` loads the
installed mod QVMs, equips goggles in an isolated config and exercises their
normal on/off command with `r_nvOverride 0`.
