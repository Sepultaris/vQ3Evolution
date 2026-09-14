# VQ3 Evolution 1.0

First Windows x64 release preparation, 2026-09-14. This is a source port, not
a copy of Quake III Arena. Supply your own licensed game and optional mod data.

## Highlights

- Vulkan raster and hardware path tracing; OpenGL fallback renderers.
- DLSS/DLAA, Ray Reconstruction, optional Frame Generation and Reflex on
  supported NVIDIA hardware. PT render scale is independent of DLAA output size.
- Three requested path-tracing samples, four bounces, adaptive sampling on,
  half-scale tracing and 2× Frame Generation by default; independent
  exposure, ambient fill, shadow softness and weapon-light controls.
- Widescreen/ultrawide presentation, horizontal FOV, VSync and independent HUD/UI scaling.
- Shift+F10 settings across Quake III, Team Arena and mods, with Exposure and
  collapsible Effects controls. Local post effects include bloom, lens dirt,
  chromatic aberration, lens distortion, bokeh DOF, motion blur, film grain,
  tone controls, color grade and vignette. The [release preset](RELEASE_DEFAULTS.md)
  includes the maintainer's effect, exposure and weapon-light settings; DOF and
  motion blur remain off.
- Improved stock materials (including traced chrome on Q3DM0's corner pillars),
  mirrors, portals, sky, fog, pickup reflections,
  Team Arena terrain blending and Urban Terror night vision.
- Corrected door light occlusion and closed-door motion history, jump-pad pulse
  texture wrapping, configuration-write handle leaks and raster/DLAA black output.

The package contains matching executable/renderers, base-game and Team Arena
native/QVM code modules, post-effect packages, runtime dependencies and notices.
No PK3 archives, mod/game textures, user configs, maps, demos or test tools are
included. The lens-dirt PNG is a post-effect asset, not Quake III game content.

## Installation

Use the packaged `README.txt` for the exact folder layout and prerequisites.
Extract into a new writable directory; keep all runtime DLLs beside the executable.
Add your Quake III `baseq3/pak*.pk3` files (including point-release updates).
For Team Arena also add its `missionpack/pak0.pk3` through `pak3.pk3`.
Run `vQ3Evolution.exe`; use Shift+F10 for engine-owned settings.
Do not combine this engine with DLLs from an earlier development build.

## Known limitations

- Software tracing and Neural Rendering remain WIP and console-only. The optional
  NRD adapter and proprietary Neural Rendering DLL are not part of this package.
- NVIDIA shutdown/restart can intermittently stall. Vulkan descriptor/presentation
  validation issues remain under investigation; a release build is not a clean
  validation certification.
- 2x Frame Generation has foreground verification; broader 3x–6x device/driver
  coverage and generated-frame visual quality need more testing. Support depends
  on the GPU/runtime. Generated FPS is not rendered FPS.
- Complex transparency/reflections, moving portals, arbitrary mod materials and
  raster object motion blur have limitations. Windows x64 is the tested target;
  no general FPS or all-mod compatibility guarantee is made.
- Optional OpenAL is not bundled; SDL audio provides the normal fallback. Windows
  HTTP/FTP downloads are disabled in this build; multiplayer networking is unaffected.

See [current status](STATUS.md) for the detailed evidence and boundaries.

## Preparation checks

The fresh 1.0 build completed with existing upstream/toolchain warnings. All
17 local post-effect shader stages compiled and validated. Repository checks,
door visibility/motion, texture caching, shared/native options, effect manifests
and SPIR-V interface regressions passed. All three renderer DLLs and all six
native game modules loaded and exposed their required entry points.

Packaged files were tested with a clean PATH (no MSYS2/SDK directories), separate
test profiles and externally supplied game content. Normal-desktop-permission
checks on 2026-09-14 completed successfully. The final preset build passed:

| Scenario | Result |
| --- | --- |
| Q3DM0 raster, packaged QVM modules | All ten effects ready; normal exit in 4.781 seconds |
| Q3DM0 official preset, packaged QVM modules | RR 960x540 to 1920x1080; active 2× FG; 258 registered defaults matched; normal exit in 13.828 seconds |
| Team Arena MPTEAM1 raster, packaged native modules | Empty profile, map/options/effects loaded; audio preset preserved; normal exit in 5.547 seconds |

The chrome pillar was visually checked from two angles, with the saved effect
preset and the clean post-RR diagnostic view. Chrome reflects the scene instead
of emitting the legacy detail picture. The 344-entry preset passed real cvar
registration/reset/config/CLI/latch tests and all supplied shader parameter bounds.

User settings were unchanged. Validation layers were off; FG was on only for the
official-preset check. These are functional checks, not performance benchmarks.
Earlier pre-preset Q3DM0 raster/PT and Team Arena startup checks also passed.
Initial official-preset test attempts had a logging-wrapper failure and an
invalid camera command; neither is claimed as pillar verification. An initial
Team Arena test read an external content-folder config; the retest explicitly
shadowed those configs with empty test files. Earlier restricted-environment runs timed
out at 45 seconds: raster/Team Arena completed the scene but stalled in NGX
shutdown, and PT did not finish RR startup. Those failures are retained in the
test record, not counted as passes. The successful ordinary-desktop runs do not
establish that the broader historical NVIDIA shutdown issue is fixed.

The package has SHA-256 checksums and an import audit of all 30 x64 executable/DLL
files; all 11 NVIDIA and three Microsoft runtime signatures verified. It has no
PK3 files, personal configs or captured gameplay. Tests cover this machine and
the listed scenes only, not every GPU, mod or Frame Generation multiplier.

## Distribution

The engine source remains GPL-covered; keep license notices and distribute the
matching source snapshot with the binary release. NVIDIA runtimes have separate
terms. See [third-party notices](THIRD_PARTY.md) and the
[publication checklist](RELEASING.md#before-publishing).
