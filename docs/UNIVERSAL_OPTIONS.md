# Universal rendering and interface options

Press **Shift+F10**, or enter `/vq3e_options` in the console. This opens an
engine-owned panel in the main menu or a loaded game, including Team Arena and
mods with their own UI and cgame modules. No replacement PK3, patched mod menu,
or new VM interface is required.

The four categories cover:

- **Display:** renderer, fullscreen, custom/desktop resolution, VSync, texture
  filtering and anisotropy through 16x.
- **Lighting:** raster/shadow/path-traced mode, samples, bounces, adaptive
  sampling, exposure, ambient light, sun/local penumbra and shadow strength.
- **NVIDIA:** DLSS/DLAA, ray reconstruction, frame generation, neural rendering,
  Reflex, sharpening and the four neural-rendering strength controls.
- **Interface:** menu and HUD scale plus legacy-module compatibility controls.

The existing base-game Graphics Options menu and console variables still work.
The engine panel is an additional entry point that does not depend on those
menus being installed. NVIDIA features still require compatible hardware and
runtimes. Dimmed controls cannot enable unsupported features; an existing
unsupported setting can still be switched off.

## Operating the panel

Use the mouse, or Tab to switch categories, Up/Down to select a row and
Left/Right to adjust it. Shift+Tab goes back one category. Enter/Apply saves the
staged changes; Escape/Cancel discards them. Display and feature changes that
need a renderer restart use the existing deferred restart/recovery path.
Other changes apply immediately when Apply is pressed.

The panel consumes keyboard, mouse and generated player movement while open;
it does not replace a mod's input catcher or change its key bindings. It does
**not pause gameplay**, including online games. Close it before resuming play.

Console entry points:

```text
/vq3e_options
/vq3e_options display
/vq3e_options lighting
/vq3e_options nvidia
/vq3e_options interface
/vq3e_options apply
/vq3e_options close
```

`apply` acts only on an already-open panel. `close` cancels. Shift+F10 is an
engine shortcut; F10 without Shift retains its normal binding.

## Shared presentation profile

Presentation settings are saved in **`fs_homepath/vq3e-rendering.cfg`**, outside
individual game folders. On a normal Windows installation this is:

```text
%APPDATA%\Quake3\vq3e-rendering.cfg
```

Normal startup loads the game's default/config/autoexec settings, then the
shared presentation profile, then startup command-line overrides. The engine
saves the profile on Apply, normal archived-config writes, game-directory
switches and shutdown. Pending renderer settings are saved without prematurely
applying their latches. Therefore changes made through the original menu or
console also carry across games. Bindings, names, servers, passwords and
mod-specific gameplay settings stay in each game's own config.

Despite its extension, the shared file is **data, not an executable script**.
Each line contains one whitelisted setting name and one validated value. It
cannot execute commands or introduce arbitrary variables. Invalid values,
extra tokens and oversized files are ignored. Writes check the completed
temporary file before replacing the previous profile (including an explicit
replace-existing operation on Windows). A failed write/replace is reported and
leaves the old profile intact. `+safe` bypasses loading and writing this profile for the
entire recovery session, including subsequent game switches.

The profile deliberately covers user-facing presentation controls, not every
experimental renderer diagnostic or mod-specific variable. The engine panel
does not silently enable RTX or change quality merely because it was opened.

### Config write failures

Changing an archived console variable also attempts to save the game's
`q3config.cfg`; a failure there is separate from the shared presentation profile.
Failed per-game file opens now print the full attempted path, C runtime error
and Windows OS error number. They also try to append the same diagnostic to
`fs_homepath/filesystem-write-errors.log`, outside the game directory. This is
useful when even `condump` cannot write inside the game directory. No permissions
are changed and config writes are not redirected to another location. If the
home root is also unwritable or the process has exhausted its file limit, only
the console diagnostic is available.

On 2026-09-12, a screenshot identified `errno 24: Too many open files` while
saving `q3history`. The path tracer's push-constant debug logger opened another
`pt_push_debug.log` stream every recorded frame, overwriting its pointer without
closing the previous stream. This was not a permissions or muzzle-control
problem. The logger now opens only when its tracked values change and always
closes the stream before returning; no file-limit increase or config relocation
is used. A full game-process restart releases handles already leaked by an old
renderer. Ray-tracing shaders and quality settings are unchanged by this fix.

The old build reproduced `errno 24` for both config saves and `condump` after
751 traced Q3Tourney1 frames (`software-lighting-audit/run-2804766ad6d74d2bbc8260e092c3da51`).
The fixed build passed the same scenario, saving brightness `0.375` in both
configs and creating the console dump (`run-162169aa44e94ddc9febd63d0b8b95cf`).
Both runs closed normally within 45 seconds, with isolated settings, hardware
tracing and NVIDIA reconstruction/Frame Generation disabled. This is a resource
lifetime regression test, not a performance benchmark. The logger fixture also
checks 10,000 unchanged frames, 4,096 setting changes and sticky open failure at
normal/release optimization. Run `tests/run-pt-push-debug-check.ps1` and
`tests/run-software-lighting.ps1 -UseSavedSettings -Mode 2 -Map q3tourney1 -Scenario rt_config_write_soak.cfg -Width 1920 -Height 1080`.

Earlier short checks ended before handle exhaustion. A saved-NVIDIA-settings
attempt stalled before its write scenario and was closed by the 45-second
guard; it is not a passing test. A separate forced Windows open-error check
verified diagnostic recording and subsequent successful writes.

## Legacy menu and HUD scaling

Source-built modules already implement `ui_scale` and `cg_hudScale`. The engine
detects their registration and leaves their drawing coordinates alone.
Unmodified modules that do not implement those controls receive centered
scaling at their rendering calls, including 2D pictures, standalone 3D previews
and cinematic previews. Menu mouse input is adjusted consistently. Full-screen
effects and world views are not shrunk.

Compatibility modes are independent:

```text
/cl_legacyUIScale 1
/cl_legacyHUDScale 1
```

- `1` (default): automatic, with no double-scaling of aware modules.
- `0`: disable engine compatibility scaling for that module type.
- `2`: force engine scaling, even if the module registers the scale variable.

This fallback cannot infer a custom mod's semantic HUD anchors or redesign a
3D interface. It preserves the mod's existing aspect/layout behavior and scales
around the screen center. Use Disabled for an incompatible custom interface.
The engine's own options panel remains usable and resolution-fitted either way.

Local VM lookup now respects game tiers: selected mod, optional base game,
then base Quake III. Loose source-built modules take priority only **within**
their own game tier, so a base-game DLL/QVM cannot displace TrueCombat's own
packed modules. Remote pure-server loading retains its approved-QVM rules.

## Verification (2026-09-08)

- Release build succeeded, including engine, native modules and renderers.
- Native fixtures execute the actual panel/profile/scaling implementation and
  local VM lookup policy, including release `-O3 -ffast-math` behavior.
- Windowed Vulkan validation checks completed for base Quake III (`q3dm0`),
  Team Arena (`mptourney1`) and installed TrueCombat 0.45 (`q3tc_tc0`). All four
  categories were captured in loaded maps; the original game/mod menus and
  overlay were also captured after disconnect.
- These runs took 8.3, 7.2 and 6.7 seconds respectively, with clean exits,
  completion markers, no Vulkan validation messages and unchanged source
  settings. They used DLAA, ray reconstruction, fixed 2 samples, FG off and
  NR off. They are compatibility checks, **not performance benchmarks**.
- TrueCombat's UI, cgame and game QVMs came from its own `pak6.pk3`; the
  compatibility scale path was active. Base/Team Arena modules reported their
  own scaling support. No live remote-server or exhaustive third-party-mod
  compatibility claim is made.
- A separate profile round-trip checked precedence over the per-game config:
  Team Arena loaded shared 75% scales over the saved 50% scales, then saved
  80% menu / 90% HUD values into the existing profile. A fresh TrueCombat run
  loaded those values and displayed the compatibility-scaled interface.
  These final-build runs completed in 6.8 and 7.2 seconds with clean Vulkan
  validation, unchanged source configs/profiles, and no leftover temporary
  profile. The test harness now reads the shared profile as well as per-game
  settings when matching the user's graphics settings.

Run `tests/run-engine-options-check.ps1` for the offline fixtures. The reusable
GPU scenario is `tests/pt_engine_options.cfg`, driven by the existing bounded
`tests/run-pt-performance.ps1` harness with its `-Game` and `-Map` arguments.
