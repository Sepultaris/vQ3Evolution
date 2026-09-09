# True Combat 0.45 widescreen menu patch

This is an optional patch to **True Combat beta 0.45 build 12**, not an engine
renderer change. It updates only `vm/ui.qvm` inside the installed mod's
`q3tc045/pak6.pk3`. It does not add an override PK3 or change textures, models,
the client-game VM, server-game VM, gameplay or saved settings. No mod payload
is included in the repository.

## Fault and correction

The original image-coordinate helper calculates `x * (xscale + bias)`. At
1920x1080, `xscale=3` and the widescreen centering offset is 240 pixels, so
images are positioned using a multiplier of 243. The first map thumbnail is
drawn at X=11664; the cursor is likewise outside the screen.

The patch changes this to `x * xscale + bias`. The mod also computes a centered
widescreen offset without reducing its horizontal scale to fit that centered
canvas. On wide displays the patch sets `xscale=yscale`, aligning images with
text and keeping all menu columns inside the screen. At 4:3 and narrower ratios,
the original scale and zero horizontal bias are retained.

The installed archive has compiled menus, not UI source. The patcher therefore
validates the exact original QVM hash and audited instructions, then edits the
bytecode. The image helper retains its instruction count. A 19-instruction
initialization helper is appended, preserving all old instruction numbers,
function pointers, jumps, jump tables and data. It adjusts scale only on wide
displays and calls the original menu cache exactly once. QVM header sizes are
updated; data, literals and BSS declarations are unchanged.

Supported original UI SHA-256:

```text
46427997abbd31ebdc5380f31bceffa83690f10c05f46e43f855a7b470c4ca41
```

Patched UI SHA-256:

```text
0856a770866faeca514308baa3e0ab20b77e894ac82e763a44f3cf4be7aaaa73
```

Unknown UI versions are rejected. Re-running the patch on its own output is a
no-op. The tool verifies every other archive entry byte-for-byte and preserves
entry ordering, dates, compression methods and metadata. The ZIP container is
rewritten, so compressed byte representations and the archive checksum can change.

## Applying and undoing

Close the game. From the repository root, inspect first, then apply:

```text
python tools/patch-truecombat-ui.py build-widescreen/release-mingw64-x86_64/q3tc045/pak6.pk3
python tools/patch-truecombat-ui.py build-widescreen/release-mingw64-x86_64/q3tc045/pak6.pk3 --apply
```

Before atomic replacement, the tool saves a byte-identical original as
`pak6.pk3.pre-widescreen.bak` beside the archive. The backup does not end in
`.pk3`, so the game does not load it. An existing different backup is never
overwritten. To undo, close the game and replace `pak6.pk3` with a copy of this
backup, retaining the backup itself. Restart/reload True Combat after either
operation. Rebuilding the engine is unnecessary.

Changing a mod archive changes its checksum. Pure multiplayer servers may require
their original approved archive; restore the backup for those servers. No pure
server validation or security checks are bypassed by this patch.

## Verification (2026-09-08)

```text
python tests/truecombat_ui_patch_check.py build-widescreen/release-mingw64-x86_64/q3tc045/pak6.pk3.pre-widescreen.bak
```

The tests execute the actual patched QVM arithmetic for 640x480, 960x720,
1920x1080, 2560x1080, 3440x1440, 5120x1440 and 720x1280. They check cursor/image
coordinates, visible map columns, unchanged non-wide behavior, stable old
instruction numbers and data, unknown-version rejection, archive preservation,
backup refusal and idempotence.

Two live 1920x1080 menu checks completed normally in 6.0 and 5.7 seconds under
independent 45-second watchdogs. They covered compiled/interpreted UI execution,
legacy scaling disabled and the saved 50% UI scale. Inspected captures show all
eight thumbnails, aligned labels, an on-screen cursor and correct hover feedback
over the second thumbnail. Frame generation and Neural Rendering were disabled;
other saved presentation values were retained. Source config/profile and
production engine/Vulkan renderer hashes were unchanged.

A diagnostic-only forwarding renderer logged draw calls and queued input only
inside the test game's SDL event stream. It did not modify rendering results
and is not part of the deployed patch. Audit captures and logs are retained in
the ignored `build-widescreen/rt-audit` directory.

These runs are **not Vulkan-validation-clean**: menu-only startup reports
`VUID-VkImageMemoryBarrier-image-03320` and `VUID-vkCmdDraw-None-09600` for the
depth/stencil image. An isolated original-mod comparison reproduced the same
two errors and completed in 5.9 seconds. They predate this mod patch and remain
separate renderer work; they are not suppressed or claimed fixed here. This is
menu correctness testing, not a performance benchmark or exhaustive mod QA.
