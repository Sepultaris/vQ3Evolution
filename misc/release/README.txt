VQ3 Evolution 1.0 - Windows x64
==============================

INSTALL
1. Extract the entire package into a new writable folder, not over an old build.
2. Copy your legally owned Quake III Arena baseq3/pak*.pk3 files into baseq3/.
   Include the official point-release updates (normally pak0 through pak8).
3. Start vQ3Evolution.exe. Keep every supplied DLL beside it.
4. Press Shift+F10 for shared engine options, including Exposure and Effects.

For Team Arena, copy its missionpack/pak0.pk3, pak1.pk3, pak2.pk3 and pak3.pk3
into missionpack/, then select TEAM ARENA or launch:
  vQ3Evolution.exe +set fs_game missionpack
If the point-release files are missing, obtain the official updates after
accepting their license at https://ioquake3.org/extras/patch-data/ .

Quake III assets and mods are NOT supplied. The baseq3/ and missionpack/
directories here contain only source-built program modules, not game assets.
The postfx/ PNG is the licensed lens-dirt effect texture, not game content.

REQUIREMENTS
64-bit Windows 10/11 and a graphics driver suitable for the selected renderer.
Vulkan path tracing needs hardware ray-tracing support. DLSS/DLAA, RR and
Frame Generation need compatible NVIDIA hardware/driver/runtime support.
Use the GPU vendor's driver installer; Vulkan is supplied by the driver.
The required MinGW and Microsoft Visual C++ runtime DLLs are included locally.
No Vulkan SDK, compiler, Python, Steam client or development PATH is required
to run this package with supplied game data. Optional OpenAL is not bundled;
SDL audio remains available as the normal fallback.

FIRST RUN
Use the console (~) and /vkinfo to confirm the Vulkan renderer and GPU.
Shift+F10 works in the base game, Team Arena and mods. The fresh-profile preset
selects 1920x1080 windowed PT, half-scale tracing, 3 requested ray samples,
4 bounces, adaptive sampling on, DLSS Quality with RR, and 2x Frame Generation.
It includes the maintainer's exposure, weapon-light and shader preferences.
Unsupported hardware features use their existing fallbacks. There is no universal
FPS guarantee. Software tracing and Neural Rendering remain console-only WIP.
The Effects master and eight effects start enabled; DOF and motion blur start
off. Enable/disable packages and expand their controls; Apply saves the choices.
Bloom is an effect. See the source docs/RELEASE_DEFAULTS.md for the preset details.

Settings normally live in %APPDATA%/Quake3, separately from this release.
Existing saved settings take precedence over source defaults. This package
contains no personal settings, and does not reset them. For an isolated profile:
  vQ3Evolution.exe +set fs_homepath "C:/Games/VQ3E-Profile"
To recover from an incompatible graphics setting:
  vQ3Evolution.exe +set cl_renderer vulkan +set r_rayTracing 0 +set r_dlss 0 +set r_dlssFrameGeneration 0
For OpenGL fallback use +set cl_renderer opengl2 instead.

KNOWN ISSUES
See RELEASE-NOTES.md. NVIDIA shutdown/restart can intermittently stall; some
Vulkan validation and complex materials/motion cases remain unresolved. FG's
multiplier is limited by GPU/runtime capabilities. Do not mix engine and
renderer DLLs from different releases. The included vQ3EvoDed.exe is optional
dedicated-server software; regular players should launch vQ3Evolution.exe.

SOURCE AND LICENSES
https://github.com/Sepultaris/vQ3Evolution
Publish the matching VQ3Evolution-1.0-source.zip alongside this folder's archive.
Keep COPYING.txt, THIRD-PARTY.md, licenses/ and the NVIDIA notices intact.
The GPL-covered engine and proprietary NVIDIA runtimes have different terms;
inclusion of notices is not a license-compatibility certification.
SHA256SUMS.txt lists the clean package files before any first-run output.
