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
