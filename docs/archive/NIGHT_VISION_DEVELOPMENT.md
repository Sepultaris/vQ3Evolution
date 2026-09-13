# Night-vision development record

Dated visual tests, repairs and unresolved failures. Current behavior and
controls are in [the NV guide](../NIGHT_VISION.md).

The Urban Terror 3.7 integration run on 2026-09-13
(`run-b0b128c99f954222b59e9da7c6ce950c` under the ignored
`build-widescreen/software-lighting-audit` directory) used the installed mod's
QVMs, RTX/RR, 640x360 tracing to a 1280x720 DLAA output, and FG/NR off.
The log confirms three normal `ut_itemuse` commands targeting goggles (item 18),
with override off. The four captures were inspected: normal colour with goggles
off, white phosphor on, unchanged white phosphor with mod preset `cg_nvg 8`, and
normal colour restored when switched off. The HUD remained unchanged. The
scenario completed before the NVIDIA shutdown stall; automatic cleanup was
needed, so this is a functional/visual pass, not a clean lifecycle pass.

The test loadout is set **before spawning**: changing gear while alive does not
replace the currently selected vest (item 17). The launcher preserves the mod's
FPS cap, terminates `activeAction` with a separator so the mod cannot concatenate
a menu command to it, and checks Quake's 32-segment startup-command limit before
launch. Earlier runs with wrong equipment or dropped startup commands are not
toggle evidence. The first raster+DLAA run delivered all toggle commands but
produced black world captures; the repair and its validation are recorded below. Native
half-resolution NV sampling and the Frame Generation input change have compiled
coverage, not a completed gameplay comparison in this integration pass.

The 2026-09-13 DLAA/RR run captured all five states at 1280x720, logged NV
activation and 121 evaluated RR frames, and reached the scenario completion
marker. White/green tint, soft masks, unmasked/debug modes and the unchanged HUD
were inspected. The existing NVIDIA SDK shutdown stall then required the
60-second guard to close the process; this is visual coverage, not a clean
process-lifecycle pass or a performance benchmark. Earlier native half-resolution
attempts skipped NV and are not appearance evidence; their validation run also
reported descriptor-update/command-buffer errors outside an active NV pass.

### Raster/DLAA repair and remaining lifecycle fault (2026-09-13)

`vk_temporal.c` now creates the sharpened output with sampled-image usage (bloom
and NV sample it), initializes all bloom/NV storage-image layouts for every
renderer mode, transitions both aspects of combined depth/stencil images while
keeping their sampled views depth-only, and supplies raster camera-motion input
in the read-only layout required by Streamline's motion-generation pass.

The repaired raster/DLAA run `run-96269c1072a54b9da86ca8fc58230889` used the actual
Urban Terror 3.7 QVM, saved settings copied to an isolated home, 1280x720 output,
and FG/NR off. The world is visible with goggles off and on; the three toggles
and four screenshots completed. There were no Vulkan validation errors.
Validation still warns that the stock NVIDIA `sl.dlss` camera-motion shader
declares RG32F storage while its internally allocated motion image is RG16F;
that warning is not fixed by these engine-side image changes.

The RTX/RR regression run `run-20330c1d69d44228b6c0e5a871e716ef` also completed
the four captures (off/on inspected) with RR active. It reported descriptor-set
updates invalidating a recorded command buffer, so it is **not** a clean Vulkan
validation pass. No path-tracing shader, quality setting or algorithm was
changed by this repair. These runs are not performance benchmarks.

A non-invasive stack capture from the current renderer shows `slShutdown`
inside NGX, waiting in `NvTelemetryAPI64!UninitializeTelemetry`. Restarting only
`NvContainerLocalSystem` with administrator approval succeeded and left the
service running, but did not resolve the wait: both subsequent tests reached
`PT_SDK_SHUTDOWN_BEGIN`, never `PT_SDK_SHUTDOWN_END`, and required their guards
(45 seconds for raster, 90 seconds including cold RTX startup). This identifies
the blocked component, not the underlying cause or proof that reinstalling
NVIDIA software will fix it. No driver, NVIDIA App files or registry settings
were changed. Saved game settings were hash-checked and remained unchanged.

The launcher now fails a shutdown timeout by default. `-AllowShutdownTimeout`
explicitly permits capture-only NV testing and prints the lifecycle warning;
it must not be used as evidence of a clean exit. The ignored
`build-widescreen/nv-lifecycle-repair` directory contains the pre-repair backup,
build log, shutdown stack and approved service-restart log.
