# Universal rendering and interface options

Press **Shift+F10**, or enter `/vq3e_options` in the console. This opens an
engine-owned panel in the main menu or a loaded game, including Team Arena and
mods with their own UI and cgame modules. No replacement PK3, patched mod menu,
or new VM interface is required.

The six categories cover:

- **Display:** renderer, fullscreen, custom/desktop resolution, VSync, horizontal FOV, texture
  filtering and anisotropy through 16x.
- **Lighting:** Raster or Path tracing, samples, bounces, adaptive
  sampling, ambient light and sun/local penumbra.
- **NVIDIA:** DLSS/DLAA, ray reconstruction, frame generation with a 2x-6x
  multiplier slider (limited by GPU/runtime support), Reflex and sharpening.
- **Interface:** menu and HUD scale plus legacy-module compatibility controls.
- **Effects:** optional local post-effect packages, enable/order controls and
  package-defined sliders; see [installation and shader contract](POST_PROCESSING.md).
- **Exposure:** all six PT exposure controls: base multiplier, automatic exposure,
  adaptation time, target and minimum/maximum limits. The former Lighting slider
  lives here; the base-game graphics menu now links to this tab instead of
  keeping a separate exposure slider/reset.

Software tracing (`r_rayTracing 1`) and Neural Rendering
(`r_dlssNeuralRendering 0`-`3`, plus its four strength controls) are WIP and
console-only. They remain in the shared profile; unrelated menu changes do not
reset them. The selectable Path tracing entry (`r_rayTracing 2`) has no WIP tag.

The existing base-game Graphics Options menu and console variables still work.
The engine panel is an additional entry point that does not depend on those
menus being installed. NVIDIA features still require compatible hardware and
runtimes. Dimmed controls cannot enable unsupported features; an existing
unsupported setting can still be switched off.

### VSync and horizontal FOV

Both controls are under **System Setup → Display** in the native base-game menu
and **Shift+F10 → Display** in every game/mod. VSync uses `r_swapInterval` (Off/On).
The native menu stages this toggle until **Apply VSync**; leaving without applying
discards the toggle. The engine panel uses its usual Apply/Cancel behavior.
Applying VSync restarts the renderer. Vulkan Frame Generation currently requires
unsynchronized presentation and overrides VSync while active; the preference is
retained for when Frame Generation is off. Driver/compositor settings can also
affect presentation.

Horizontal FOV uses the existing `cg_fov`, from 1–160 degrees in half-degree menu
steps (default 90). These are **4:3 base horizontal degrees**: the source-built
Quake 3/Team Arena cgame expands the horizontal view on wider screens. For example,
90 becomes about 106.3 degrees at 16:9. This does not change the projection, zoom,
or server fixed-FOV behavior. The native slider applies immediately; Shift+F10
stages it until Apply, without restarting. Both entry points and the console
share the same persisted value. Mods using `cg_fov` receive it but may interpret
or restrict it differently; mods with a different FOV variable are not overridden.

Frame Generation has separate Off/On and multiplier controls. The multiplier
uses integer steps from 2x to 6x, capped to the reported device/runtime limit;
it is dimmed when FG is off or the device supports only 2x. Applying a changed
multiplier requires a renderer restart. A higher console/saved request is
preserved even if the runtime must use a lower value. Use `nvidia_info` to see
the requested, configured and maximum multipliers. Generated presentations are
not additional rendered game frames; see [verification limits](DLSS.md#multiplier-verification-2026-09-13).

## Operating the panel

Use the mouse, or Tab to switch categories, Up/Down to select a row and
Left/Right to adjust it. Shift+Tab goes back one category. Enter/Apply saves the
staged changes; Escape/Cancel discards them. Display and feature changes that
need a renderer restart use the existing deferred restart/recovery path.
Other changes apply immediately when Apply is pressed.

Each shader in Effects has a collapsible settings group. Click its name or
`[+]`/`[-]`, or select its header and press Space, to show/hide its order and
parameter sliders. Its On/Off control stays visible and is independent of the
fold state. Folding never disables an effect or discards pending edits. Groups
start collapsed when the panel opens and retain their state while switching tabs.

Exposure sliders use logarithmic spacing to keep small values practical across
their full renderer-supported ranges; arrows make fine fixed-step adjustments.
**Reset Exposure** stages all six source defaults; Apply commits them and Cancel
discards them. Base exposure still affects the automatic-exposure baseline;
smaller adaptation time means a faster response. The renderer's exposure
algorithm/defaults are unchanged. These controls affect the path-traced scene,
not HUD/menus. Post-effect grading exposure is a separate later operation and
remains with its individual shader in Effects.

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
/vq3e_options effects
/vq3e_options exposure
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

## Display control verification (2026-09-13)

Native-menu and engine-panel fixtures passed normal and release/fast-math builds:
opening/cancel does not change settings, FOV applies without a restart, VSync
requests an explicit/deferred restart, and both preferences persist in the shared
profile. NVIDIA-enabled and SDK-free release builds passed.

The isolated `-OptionsLayout` scenario captured both menus and a 90→110→90 FOV
sequence in raster and half-resolution DLAA/RR. Captures show the wider field of
view and readable controls. Saved user profiles were hash-checked unchanged.

| Run under `build-widescreen/postfx-audit/` | Result |
| --- | --- |
| `run-70ce9106fc2b4af3a92ba7863270245f` | SDK-free raster, VSync on: selected FIFO, validation log contained only instance begin/end, normal exit in 3.6 s. |
| `run-488564c296cf4d41b16bb7eb3822b665` | PT/DLAA/RR, VSync off: selected immediate presentation, normal exit in 6.3 s. Validation off. |

FG was off in both checks. These are functional/menu checks, not frame-pacing or
performance benchmarks. Mod-specific FOV restrictions remain mod-owned.

## Exposure and folding verification (2026-09-13)

The engine options fixtures passed at normal and release fast-math optimization,
including six exposure controls, logarithmic ranges/fine adjustment, staged
reset, independent shader enable/fold state, scroll hit mapping, hidden edits,
Apply/Cancel and shared-profile restore. Native base-game UI and QVM builds
also succeeded. A windowed DLAA/RR PT layout check exited normally in 5.1 seconds
(`postfx-audit/run-8275540bbf784008a13c5f7f02872048`). Exposure, Lighting,
collapsed Effects and the classic graphics shortcut captures were inspected.
The run used isolated settings and FG off; source configs were unchanged.
Vulkan validation was disabled; this is not a renderer-validation or performance
claim. See `tests/run-postfx-check.ps1 -OptionsLayout` for the reusable scenario.

## Earlier compatibility verification (2026-09-08)

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
