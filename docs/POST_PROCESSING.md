# Native post-effect packages

The Vulkan renderer can run locally installed post effects after its existing
reconstruction, tone mapping/sharpening and night-vision stages, before
HUD/menu composition. Both raster and path-traced scenes use this insertion
point. The path-tracing integrator and RR inputs are unchanged. When effects
run, the final processed scene is also the HUD-less input supplied to Frame
Generation; actual FG combinations still need their own runtime verification.
Color-warp effects do not warp FG's depth/motion guides, so FG-on warp quality
is not covered by the tests below.
The [RR-only debug view](RAY_RECONSTRUCTION.md#rr-only-debug-view),
`r_pathTracingRRDebug 1`, bypasses this chain without changing its settings.

This is a first-version native package system, **not ReShade compatibility**.
Vulkan GLSL is compiled to SPIR-V before installation; the game does not compile
GLSL at runtime or load ReShade `.fx` files. Compute and fullscreen graphics
(vertex plus fragment) passes can be mixed in one effect. World geometry shaders
are outside this interface.

## Install and enable

Install a package's `.effect` manifest, referenced `.spv` files and optional PNG directly in
`postfx/` under either `fs_homepath` (the global Quake3 user directory) or
`fs_basepath` (normally the release directory beside `vQ3Evolution.exe`). The
home directory takes precedence. These are global folders, **not**
`baseq3/postfx` or a mod's folder. The engine does not discover packages inside
PK3s or automatically downloaded mod assets. Local effects and their settings
remain available across base Quake III, Team Arena and mods.

The source repository includes Color grade (compute), Vignette, Chromatic
aberration, Lens distortion, Lens dirt, Bloom, Bokeh depth of field, Motion blur, Film grain and Tone controls (vertex/fragment)
in [postfx](../postfx). Build/install their packages
with a Vulkan SDK, from the repository root:

```powershell
tools/compile-postfx.ps1 -VulkanSDK $env:VULKAN_SDK
```

The default destination is `build-widescreen/release-mingw64-x86_64/postfx`.
Use `-SourceDirectory` and `-OutputDirectory` for another package or installation.
The helper compiles and validates every shader before copying anything to the
destination; compile failures leave installed files untouched. It does not
rebuild the game. PNG header/dimension limits are checked before installation;
the original image bytes are copied unchanged. Keep the game closed while replacing packages, or reload
after copying has completed.

Open **Shift+F10 > Effects** (or `/vq3e_options effects`). Enable the post-effect
system and Apply; this first activation requires a renderer restart. Each
package has a visible enable switch and a collapsible settings group. Click
the shader name or `[+]`/`[-]` (Space on a selected header) to show/hide its order
and parameter sliders. Groups start collapsed; hiding settings does not disable
the shader or discard edits. Lower order runs first; equal orders sort by package ID. Scroll with
the mouse wheel or Up/Down to reach additional rows. Apply saves edits to the
shared rendering profile; Cancel discards them. Parameters and per-effect
enable/order changes do not require a restart.
PT scene exposure and automatic-exposure settings now live in **Shift+F10 >
Exposure**, not Lighting. A package's own grading/exposure parameters remain
in its shader group: they operate after PT tone mapping and also support raster.

The [1.0 release preset](RELEASE_DEFAULTS.md) enables the master and all supplied
effects except DOF and motion blur. Known package settings/order come from the
engine preset; an unknown/custom package starts off and uses its manifest's
parameter defaults. Saved settings always take precedence. Legacy
bloom preferences are imported as described below; the old always-separate
bloom stage no longer runs, including when the master is off.
With the master off there are no effect pipelines or ping-pong images allocated
and no effect draw/dispatch work. Enabling it allocates two full-resolution
RGBA8 intermediate images (about 16 MiB at 1080p, excluding allocation overhead)
and prepares package pipelines. Raster without other temporal features also
needs the renderer's existing offscreen scene targets. Effects add GPU work;
this feature is not a path-tracing performance optimization.
Installing a version-3 package adds two quarter-width/height RGBA16F targets
(about 2 MiB together at 1080p) while the master is on. These targets are shared
between reduced-resolution effects; the original effect input remains untouched.
Declared textures are loaded when the master is on, even if their individual
effect is off; the included 4096x2048 dirt image uses another 32 MiB of GPU memory.
Large PNG decode/upload buffers use temporary heap memory, not the engine's
small fixed zone. They are released after upload; GPU textures are released on
renderer shutdown/reload.

## Bloom, lens, grain and tone controls

These effects are optional, individually enabled in **Shift+F10 > Effects**:

| Package | Controls |
| --- | --- |
| `bloom` | Intensity; highlight threshold; blur radius in output pixels; glow-only debug (0/1) |
| `chromatic` | RGB separation in output pixels; edge falloff |
| `lensdistortion` | Barrel/pincushion amount; zoom/crop; black-border softness |
| `lensdirt` | Intensity; highlight threshold; light spread; texture zoom; dirt contrast; dirt color/saturation; soft glare; scatter gain |
| `bokehdof` | Focus distance; defocus strength; maximum blur radius; center autofocus; highlight threshold/gain; aperture blades; sample count |
| `motionblur` | Strength; shutter angle; maximum streak; sample limit; depth tolerance; minimum motion; shutter reference FPS |
| `tonemap` | Exposure; contrast; shadows; highlights; gamma; saturation; black point; highlight shoulder |
| `filmgrain` | Grain amount; grain size in output pixels; animation off/on (0/1) |

### Bloom

Bloom is now an ordinary four-pass version-3 package in **Shift+F10 > Effects >
Bloom**. Enable the post-effect master and Apply if it is not already running;
then enable Bloom and expand its settings. Package enable/controls/order are
live after Apply. The separate built-in bloom pass, shaders and four dedicated
images have been removed. Bloom shares the chain's two quarter-size RGBA16F
images with Lens dirt; there is no second bloom underneath the package.

Release defaults are Intensity 0.325, Highlight threshold 0.875, Radius 14 output pixels
and Glow only off. The radius is Gaussian sigma (three-sigma support), adjustable
from 0 to 32; zero skips spreading, not highlight extraction. The quarter-size
prefilter/upsample still has a small footprint at radius zero. Intensity zero
is an exact color bypass, including the debug view. Threshold 1 uses a 0.98
extraction knee to avoid division by zero, as in the former built-in shader.

With `r_postfx 1` applied and the renderer restarted, console equivalents are:

```text
r_fx_bloom_enabled 1
r_fx_bloom_strength 0.6
r_fx_bloom_threshold 0.5
r_fx_bloom_radius 4.5
r_fx_bloom_debug 0
r_fx_bloom_order 10
```

Bloom's release order is 10; custom packages default to 50. The example places bloom
at 10. It processes reconstructed display color after NV and before the HUD,
not scene-linear HDR. It retains the old squared highlight extraction and
bright-core composite, but its continuous separable blur is not pixel-identical
to the former half/quarter-resolution kernel. PT exposure and tone mapping have
not changed. `r_pathTracingRRDebug 1` bypasses it with the other post effects.

On startup, valid saved `r_postBloom`, `r_postBloomStrength` and
`r_postBloomThreshold` values initialize the corresponding new enable, strength
and threshold preferences **only if those preferences are missing**. An explicit
new Off value wins. Old debug mode is not imported. Old names are retired,
not live aliases; use the new controls for subsequent edits. Migration does not
turn on `r_postfx`, so enabling the master may be necessary to see imported
bloom. User configuration files are not rewritten by the migration itself;
new preferences use the normal archived/shared-profile save mechanism.

### Lens dirt and film grain

Film grain adapts the supplied additive monochrome-noise example to Vulkan GLSL.
Amount is independent of Size: Size changes noise-cell dimensions (1–10 output
pixels), not brightness. The release preset uses Amount 0.02 and Animate on, with the effect enabled. An integer pixel hash avoids sine precision bands;
Animate changes the seed each rendered frame instead of stretching UVs by time.
The fixed pattern stays on the screen as the camera moves. Noise is added equally
to RGB, clipped to display range, and preserves input alpha. It runs after RR,
so reconstruction does not remove it, and before the HUD. Translucent HUD edges
can still reveal the scene/grain beneath them.

Use `r_fx_filmgrain_enabled 1`, `r_fx_filmgrain_amount 0.05`,
`r_fx_filmgrain_size 1` and `r_fx_filmgrain_animate 1` for animated fine grain
with the post-effect master enabled. An order of 60 places it after the suggested
lens stack below. The animation advances on real rendered frames, not on each
generated frame; FG-on grain behavior has not been tested. No engine rebuild is
required to install this package; use `postfx_reload` to rescan it.

Lens dirt uses the supplied [lensdirt.png](../postfx/lensdirt.png) as a static
screen-space scattering mask. A 4x4 highlight prefilter, horizontal and vertical
Gaussian blur, and final composite replace the old sparse nine-tap gather that
stamped displaced copies of bright lights. The continuous blur runs at quarter
width/height in floating-point targets; the final pass reads the preserved
original color. Larger spread increases the blur footprint without leaving gaps
between taps. It preserves the dirt image's aspect ratio with a centered crop
and keeps its colors; set Dirt color to zero for monochrome.

Soft glare adds smooth haze alongside the texture; Scatter gain raises both
contributions as a wide blur dilutes small highlights. For broad, lens-like
scattering, start with Light spread 0.12–0.20, Scatter gain 2–4 and Soft glare
0.15, then tune Intensity/Threshold for the scene. Spread is a fraction of image
height, not a pixel count. Its release default is 0.035 (manifest fallback 0.12); existing saved values
are not overwritten. Intensity zero is an exact bypass, including glare.
Dark areas outside illuminated blur footprints do not receive a constant overlay.
This is a display-color approximation, not an HDR bloom-buffer input or a full
optical lens-flare model. It does not synthesize the reference image's flare
ghosts/streaks; broad glare illuminates the supplied dirt texture.

The included PNG was supplied by the project owner on 2026-09-13 and copied
without pixel changes. On 2026-09-14 the owner explicitly confirmed permission
to redistribute it in the release. See [asset provenance](THIRD_PARTY.md#post-effects);
this does not relicense the image as public domain.

### Bokeh depth of field

`bokehdof` samples actual scene depth: raster depth for raster scenes and the
tracer's depth for PT, with camera-projection linearization and reconstruction
jitter compensation. It uses a deterministic disk gather with 16–128 samples,
round or 3–8-sided apertures, and an optional bright-highlight boost. The source
release settings are 64 samples, a 6-output-pixel maximum blur and center
autofocus on, with manual focus retained at 376
game units. Aperture values 0–2 in the Blades control select round bokeh.
Defocus strength controls how quickly blur grows away from the focused distance;
this is an artistic game-unit model, not physical focal-length/f-stop controls.
Zero defocus strength or zero radius preserves the original image.

Enable it in **Shift+F10 > Effects > Bokeh depth of field**, or, with the master
already active:

```text
r_fx_bokehdof_enabled 1
r_fx_bokehdof_autofocus 1
r_fx_bokehdof_radius 12
r_fx_bokehdof_samples 64
```

Autofocus reads depth at the screen center; it is not object tracking and has no
focus-rack smoothing. For fixed focus, set autofocus to 0 and adjust
`r_fx_bokehdof_focus`. Set the effect order to 5, before distortion/chromatic
warps, since those effects do not warp the depth guide. The HUD stays sharp.
The effect defaults off and adds sampling cost; reduce radius/sample count if
needed. Small bright bokeh can look undersampled at low quality. It rejects
in-focus foreground contamination, but a single scene-depth layer cannot
reconstruct hidden backgrounds or separately focus every reflection, particle,
glass layer or portal view. FG-on DOF quality has not been verified.

Visual/feature reference: Martins Upitis' [DoF with bokeh v2.4](https://github.com/orthecreedence/ghostie/blob/master/opengl/glsl/dof.bokeh.2.4.frag),
whose source declares CC BY 3.0. The included Vulkan shader is independently
written under GPL-2.0-or-later, not a copy or a drop-in port of that shader. It
does not implement its physical lens model, focus-debug overlay or internal
chromatic-fringing option.

### Tone controls and ordering

Tone controls grade the already tone-mapped image; they cannot recover highlights
clipped earlier in the renderer. Neutral manifest values preserve the original image; the release preset uses
+0.4 exposure and a 0.025 highlight shoulder.
For an ordered lens stack, one choice is Motion blur 2, Bokeh depth of field 5, Bloom 10, Tone controls 15, Lens distortion 20,
Chromatic aberration 30, Lens dirt 40, Vignette 50. These are suggestions, not
automatic changes to saved preferences. Large warps can reveal black borders;
increase Zoom to crop them. The HUD and menus remain unwarped.

### Motion blur

Enable **Shift+F10 > Effects > Motion blur** and set its order to 2, before DOF,
lens distortion and fixed lens dirt. It reads the existing camera/tracked-object
motion vectors in PT. Raster computes camera motion from depth and the current
and previous camera matrices; it does **not** yet have raster per-object motion.
The shader samples the current reconstructed scene along a centered shutter
interval. It does not accumulate previous color, so it does not leave history
trails when movement stops. Depth and velocity rejection reduce bleeding across
foreground silhouettes, and the HUD is composed afterward and stays sharp.

Defaults are strength 1, shutter angle 180 degrees, maximum streak 32 output
pixels, and up to 24 samples. Tap count decreases for short motion. The effect
itself defaults off. With the post-effect master already enabled:

```text
r_fx_motionblur_enabled 1
r_fx_motionblur_order 2
r_fx_motionblur_shutter 180
r_fx_motionblur_radius 32
r_fx_motionblur_samples 24
```

Zero Strength, Shutter angle or Max streak bypasses blur. A larger shutter angle
gives longer streaks; 180 means half the motion between rendered frames. Depth
tolerance controls how freely blur crosses depth changes; the default 0.05
rejects markedly different surfaces. Min motion defaults to 0.5 pixels to keep
tiny movements sharp. Shutter reference FPS 0 follows the actual rendered frame
interval. A nonzero value (for example 60) instead targets a fixed exposure
duration, with an 8x normalization cap for extreme frame rates. It never uses
the generated/presented frame count. Discontinuous history, paused time, long
stalls, teleports and large camera cuts disable blur for that frame.

This is an optional, single-layer screen-space approximation, not ray-traced
shutter integration. It cannot reveal hidden backgrounds or perfectly sweep
thin moving objects beyond their current silhouette. PT object/reflection
coverage inherits the existing guide limitations; untracked mod geometry gets
camera motion only. High maximum streaks may need more samples. FG-on quality
and a broad moving-object/weapon/transparency suite remain unverified. No change
to the tracer, RR algorithm or saved user quality settings is required.

Console controls:

| Control | Meaning |
| --- | --- |
| `r_postfx 0` / `1` | Master switch; release default `1`, restart required |
| `postfx_info` | List package readiness, requested enable state, order and pass count |
| `postfx_reload` | Deferred renderer restart to rescan/reload installed packages |
| `r_fx_colorgrade_enabled 1` | Example per-effect switch; enabled in the release preset |
| `r_fx_colorgrade_order 25` | Example order, integer 0–100; release order 7 (custom packages default 50) |
| `r_fx_colorgrade_saturation 0` | Example manifest-defined parameter |

Reload currently uses the ordinary renderer restart, not transactional hot
reload. Keep backups when replacing a working package. Missing/invalid
manifests are skipped with a console diagnostic; a texture/shader/pipeline failure
bypasses the entire effect and is shown in `postfx_info` and the Effects tab.

## Manifest versions

The file stem is the stable package ID, for example `colorgrade.effect`:

```text
version 1
name "Color grade"
pass compute "colorgrade.comp.spv"
param exposure "Exposure (stops)" 0 -2 2 0.05
param saturation "Saturation" 1 0 2 0.05
param contrast "Contrast" 1 0.5 1.5 0.025
```

A graphics pass names a vertex shader and then a fragment shader:

```text
pass graphics "fullscreen.vert.spv" "vignette.frag.spv"
```

Version 1 remains supported unchanged. Version 2 adds one optional local PNG
shared by the package's compute/fragment passes:

```text
version 2
name "Custom texture overlay"
texture "overlay.png"
pass graphics "fullscreen.vert.spv" "overlay.frag.spv"
```

Textures must be lowercase `.png` leaf filenames, at most 32 MiB compressed,
with each dimension in 1–4096. They decode to RGBA8 without world-material
picmip, brightness/gamma scaling or the world renderer's 2048-pixel limit.
There is no automatic sRGB conversion or mip generation. Missing/invalid
textures bypass the whole effect, including when its shader would otherwise load.

Version 3 adds a restricted reduced-resolution graphics chain. It requires
`downsample 4` and two to four graphics passes. All but the last pass write
quarter-width/height RGBA16F; the last writes full-resolution RGBA8. Binding 3
always samples the preserved input to the whole effect, not the previous pass.
For example, the included dirt package uses:

```text
version 3
name "Lens dirt"
texture "lensdirt.png"
downsample 4
pass graphics "fullscreen.vert.spv" "lensdirt_prefilter.frag.spv"
pass graphics "fullscreen.vert.spv" "lensdirt_blur_h.frag.spv"
pass graphics "fullscreen.vert.spv" "lensdirt_blur_v.frag.spv"
pass graphics "fullscreen.vert.spv" "lensdirt.frag.spv"
```

Version 4 adds **read-only scene depth** to full-resolution chains. It requires
`depth scene`, enables binding 4 and extends the push constants by one `vec4`.
It cannot be combined with version 3's reduced targets/original-color binding.
An unavailable scene-depth input bypasses the effect rather than sampling a
stale image. The bokeh package uses:

```text
version 4
name "Bokeh depth of field"
depth scene
pass graphics "fullscreen.vert.spv" "bokehdof.frag.spv"
```

The above resource examples omit parameters; use the shipped manifests for
complete runnable effects. Versions 1 and 2 remain supported unchanged.

Version 5 adds read-only motion guides to full-resolution chains. It requires
both `depth scene` and `motion scene`, enables binding 5 and extends the push
block to eight `vec4`s (128 bytes). It does not enable downsampling or binding 3.
See `motionblur.effect` and `motionblur_guides.glsl` for a complete example.

Each parameter specifies `id "label" default minimum maximum step`. Parameter
declaration order maps to the eight float slots supplied to every pass in that
effect. Passes execute in declaration order; each reads the previous pass's
color output. Separate effects are ordered using their order controls.

Limits are 16 effects, four passes per effect, eight parameters per effect,
16 KiB per manifest and 1 MiB per shader file. IDs are 1–23 lowercase ASCII
letters/digits. Parameter IDs `enabled` and `order` are reserved. Labels may
be quoted; `//` comments are supported. Paths must be lowercase leaf filenames
(letters, digits, `.`, `_`, `-`), with no traversal or directory components.
Unknown fields, duplicate parameters, invalid ranges and non-finite values are
rejected. Manifests are data, never executed as config/console commands.

## Shader interface

Compile for Vulkan 1.0 / SPIR-V 1.0 with entry point `main`. The interface is:

| Resource | Contract |
| --- | --- |
| Set 0, binding 0 | Combined `sampler2D`, previous pass's color; clamp-to-edge, linear sampling, mip 0 |
| Set 0, binding 1 | Compute only: write-only `rgba8 image2D` output |
| Set 0, binding 2 | Version 2–5 with `texture` declared only: combined `sampler2D` PNG; clamp-to-edge, linear, mip 0; compute/fragment only |
| Set 0, binding 3 | Version 3 only: combined `sampler2D`, preserved original effect input; clamp-to-edge, linear, mip 0 |
| Set 0, binding 4 | Versions 4–5: combined `sampler2D`, read-only scene depth; clamp-to-edge, nearest, mip 0 |
| Set 0, binding 5 | Version 5 only: combined `sampler2D`, previous-minus-current normalized-UV motion; clamp-to-edge, nearest, mip 0; only populated by the tracer |
| Graphics output | Location 0, `vec4`; full-resolution RGBA8 except version-3 prepasses (quarter-size RGBA16F) |
| Graphics input | No vertex buffers; three vertices using `gl_VertexIndex`; sample vertex shader supplies location-0 UVs |
| Compute group | Fixed `8x8x1`; bounds-check dispatch coordinates for non-multiple-of-eight resolutions |
| Push constants | Versions 1–3: three `vec4`s; version 4: four; version 5: eight; consecutive 16-byte offsets from zero |

```glsl
layout(push_constant) uniform Effect {
    vec4 extentTimeFrame; // output width, output height, seconds, frame index
    vec4 values0;         // parameters 0-3
    vec4 values1;         // parameters 4-7
    // Version 4 only: append vec4 projectionJitter;
} effect;
```

Unused parameter slots are zero. Shaders may omit resources they do not use.
Use normalized coordinates to sample the input: it may be smaller than output
resolution when reconstruction is disabled or unavailable. `extentTimeFrame.xy`
is the current pass's output extent, including reduced version-3 prepasses.
Do not assume pixel-matched input dimensions.

For version 4, `projectionJitter.xy` contains camera P[10] and P[14]; `.zw` is
the normalized offset to add to color UVs when sampling raw depth after
reconstruction. Positive linear view distance is `P[14] / (rawDepth + P[10])`;
handle the far-plane zero denominator explicitly. See `bokehdof.frag` for the
complete contract. Depth can be smaller than the output, especially with PT
render scaling. Menus have no scene-depth effect execution. Depth images remain
read-only and their owning renderer layout/access is restored after the effect.

Version 5 appends `motionInfo`, `previousClipX`, `previousClipY`, `previousClipW`
at offsets 64, 80, 96 and 112. `motionInfo` holds: traced vectors present, history
valid, rendered-frame seconds, reconstructed-color flag. The three rows map
current unjittered homogeneous clip coordinates to previous clip x/y/w; this
allows SDK-free camera reprojection without another GPU buffer or image pass.
For v5, `projectionJitter.zw` always contains negative input jitter in UV units:
add it when sampling guides for reconstructed color, or subtract it from raw
color UVs to obtain unjittered clip coordinates. V4 retains its previous contract.
Never use motion when history is invalid, or interpret raster's zero/object input
as a complete camera vector. Motion images are read-only; owner layouts and
access are restored, including the NVIDIA resource contracts.

Color is the renderer's existing tone-mapped display color in normalized 0–1
storage, not scene-linear HDR radiance. The engine inserts no additional gamma
conversion. Preserve input alpha unless an effect explicitly requires otherwise.
Compute passes must write every in-bounds pixel; graphics targets are cleared
before drawing. Each pass uses separate input/output images, with explicit
read/write transitions. The HUD and menu are not part of the sampled scene.

The loader checks the shader envelope, execution stage/workgroup, descriptor
interface and push-constant layout. It does **not** implement full SPIR-V
validation or sandbox arbitrary GPU code. Install trusted packages only and
run `spirv-val` when building them. Bad or excessively expensive GPU programs
can still stall/reset a driver. See the official Vulkan documentation on
[shader modules](https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/02_Graphics_pipeline_basics/01_Shader_modules.html)
and [push constants](https://docs.vulkan.org/guide/latest/push_constants.html).

Not yet supported: normal inputs, HDR insertion points, history
textures, arbitrary intermediate sizes/formats beyond version 3, multiple auxiliary inputs/3D LUTs, branching
pass graphs, named presets, runtime GLSL compilation or live shader hot reload.
Existing game-material shaders are not interchangeable with post-effect passes.

## Checks

Offline parser/interface fixtures and the existing engine-options fixture cover
malformed manifests, path/command rejection, actual compiled shader interfaces,
menu scrolling, apply/cancel and shared persistence. The guarded
`tests/run-postfx-check.ps1` scenario captures effects off, compute, an ordered
compute/fragment chain, a mixed-pass package and the Effects menu. It is a
functional test, not a saved-quality performance benchmark. Use `-Mode raster`
or `-Mode pt`; PT selects DLAA/RR, fixed two samples and FG off. Runs use isolated
settings and a 45-second default owned-process deadline (`-TimeLimitSeconds`
accepts 15–90 seconds). `-RuntimeDirectory` can select
an SDK-free build for independent raster tests.

### Bloom-package verification

On 2026-09-13 both release builds and the `-Bloom` scenario passed:

| Run | Result |
| --- | --- |
| SDK-free raster, `run-dc5c9254e0c34947a2da23fd50c02724` | Normal exit in 3,688 ms; raw Vulkan validation log contained only instance begin/end markers |
| Half-resolution DLAA/RR PT, `run-42528a5bd29d49189b9a059b9fe751ba` | Normal exit in 6,625 ms; FG and validation off |

Analytic-chart/dense-Gaussian checks matched composite, glow-only, radius-zero
and maximum-radius output to mean errors below 0.024/255. Threshold, intensity
zero, stable output, order changes, two v3 packages sharing scratch images and
sharp ammo digits passed. Review sheets confirmed smooth halos and Bloom in
the Effects listing. The initial HUD assertion also included an animated 3D
icon; it was corrected to test only stationary ammo digits, not relaxed to
allow bloom on the HUD. All shader modules passed compilation, `spirv-val` and
the loader-interface fixture. Migration and panel checks passed normal and
fast-math builds. Saved user settings were unchanged.

The PT RR-only routing regression, `run-cb51f2d342fc4085be5bdfe1247a2180`,
also exited normally in 6,625 ms and passed the independent routing/recovery
checker with Bloom's glow-debug enabled. These are bounded functional checks,
not performance measurements, proof of FG-on quality or full PT validation.

### Motion-blur verification

Version-15 motion checks on 2026-09-13:

| Run | Result |
| --- | --- |
| SDK-free raster, `run-0d356d1d2a3b4515ae0739e697bc2f25` | Normal exit in 4,125 ms; raw validation log had only instance begin/end markers |
| Half-resolution DLAA/RR PT, `run-10482cae57ae44858060a32e16ce344d` | Normal exit in 7,313 ms; 480x270 guides behind 960x540 output; FG and validation off |

The independent image checker verified stationary/stop identity, no visible
RR-jitter-induced blur, three zero-control bypasses, camera vectors, shutter
angle, maximum streak and the sharp colored HUD. CPU motion/depth gathering
matched the moving grid to mean errors below 0.95/255. Live-scene captures and
the Effects listing were inspected. Matrix fixtures exercise camera rotation,
translation, perspective, stationary identity, singular transforms and pause/cut
guards in normal and fast-math builds. Packages and shader interfaces passed,
and SDK-enabled/SDK-free release builds succeeded. Saved settings were unchanged.

The first stationary scenario was invalid: the player still translated after
placement near Q3DM6's jump pad. The corrected scenario uses noclip and records
identical view positions before accepting stationary results. This was a test
setup correction, not a renderer fix or a relaxed image tolerance. Full object,
weapon and transparency coverage and FG-on quality are not established by these
camera tests. These are functional checks, not a performance claim.

### Continuous lens dirt and bokeh verification

On 2026-09-13 the version-14 builds completed the extended `-LensEffects`
scenario with saved-settings hashes unchanged:

| Run | Result |
| --- | --- |
| SDK-free raster, `run-07c7cf587a954a15b4e474fe768e1e26` | Normal exit, 5,703 ms; raw Vulkan validation log contained only instance begin/end markers |
| Native DLAA/RR PT, `run-bba18537fdee4490bad09d0f2cbbd534` | Normal exit, 8,938 ms; RR active; validation off |
| Half-resolution DLAA/RR PT, `run-805488173e254b5c993c96f4467380cd` | Normal exit, 8,704 ms; RR confirmed 480x270 to 960x540; validation off |

Independent CPU checks matched the continuous dirt blur at normal and maximum
spread (mean error 0.014/255 and 0.192/255), rejected the former nine-copy gather,
and checked that an extra preceding compute pass did not overwrite original
color. PNG-based composition matched to 0.252/255 in raster and native PT.
Packed real scene depth drove independent CPU bokeh predictions: manual/center
focus, round/hexagonal apertures and highlight gain matched within 0.334/255 mean
error across all three runs. Zero defocus/radius preserved scene color, the HUD
stayed sharp, and inspected real-scene captures showed depth-dependent blur.
Existing lens/tone and film-grain checks passed in raster/native PT as well.

Parser, package, compiled-interface and normal/fast-math engine-options checks
passed; both SDK-enabled and SDK-free release builds succeeded. New shader
modules passed `spirv-val`. These are short functional runs, not performance
benchmarks, proof of correct layered transparency/foreground motion, FG-on
quality, or whole-renderer validation certification. Earlier shutdown failures
remain open. The detailed test recipes are in [Testing](TESTING.md#local-post-effect-checks).

### Earlier texture/grain evidence

The original `-LensEffects` scenario used an isolated chart for chromatic
aberration, barrel/pincushion distortion, PNG-based lens dirt and tone controls.
On 2026-09-13 the earlier texture-aware builds passed:

| Run | Result |
| --- | --- |
| SDK-free raster, `run-3fec3fe6a6c1432984b0844a04006e3d` | Normal exit, 3,656 ms; raw validation log contained only instance begin/end markers |
| DLAA/RR PT, `run-f11795c780e84b908e35e6327dad7e33` | Normal exit, 6,406 ms; RR evaluation active; validation disabled |

Both loaded the original PNG at 4096x2048. Inspected captures and
`postfx_lens_image_check.py --texture postfx/lensdirt.png` passed neutral identity,
channel separation, both warps, static/light-driven dirt, tone controls and HUD
exclusion. An independent CPU reconstruction using the supplied PNG matched
GPU dirt output to mean error 0.210/255 in both tests. PNG bytes matched the
supplied file by SHA-256. Saved-settings hashes were unchanged.
Parser, package, shader-interface, PNG decoder and engine-options fixtures passed;
both SDK-enabled and SDK-free engine/renderer builds succeeded. These short
functional runs are not performance benchmarks, FG-on tests, or proof that the
intermittent NVIDIA shutdown issue is resolved.

Film-grain verification on 2026-09-13 used
`run-e3427fa48b4046d4890030890e5d0cfb` (SDK-free raster, normal exit in 4,438 ms,
validation log had only begin/end markers) and
`run-c5cb574697df4e51b0f657e161e2d82c` (DLAA/RR PT, validation off, normal exit
in 8,265 ms). The grain image checker passed zero identity, exact static
pixel-hash output, one/four-pixel cells with equal amplitude, changing animated
seeds and opaque HUD exclusion. The existing lens/PNG image checks also passed
in both runs. Saved settings were unchanged. FG was off; no performance gain
or FG-on grain quality is claimed.

### Earlier base-package evidence

Recorded on 2026-09-13: the SDK-free raster run
`run-dcee0cc7ddb34d37bc1bd892f261a7b3` exited normally in 2,985 ms. All three
packages loaded, compute desaturation, the fragment vignette and mixed passes
were visible, the HUD retained its color, and the Effects panel was inspected.
The raw validation log contained only instance begin/end markers. Saved-settings
hashes were unchanged. `tests/postfx_image_check.py` independently checked these
captures (Pillow required). This is one-scene functional coverage, not a
performance benchmark or whole-renderer certification.

An earlier NVIDIA-enabled raster run reproduced the existing NGX shutdown
stall and required its guard. Two 45-second PT/RR attempts did not reach the
capture phases: the log reached RR feature creation only near the deadline.
Those runs do not establish PT image correctness or a lifecycle pass.

The final no-validation PT/RR run, with an explicitly announced 75-second cap,
`run-e49990f367004cf8bf42b5bb6b2285ad`, completed all five captures and the scenario.
RR evaluation was active; inspected captures and the image checker confirmed
compute desaturation, fragment vignette, mixed-pass color and HUD exclusion.
It entered SDK shutdown at 51,291 ms, stalled while shutting down NGX, and was
terminated by the guard at 75,172 ms. Saved settings were unchanged. This is
successful image-path evidence, **not** a normal-exit or PT-validation pass.
No Frame Generation-on test, performance gain or NVIDIA lifecycle fix is claimed.

The engine/renderer API is now version 15 for texture, reduced-pass, scene-depth and motion
metadata plus paired local-file callbacks. Rebuild/deploy the executable and renderer DLLs together. Mod VM
interfaces and game-data formats have not changed.
