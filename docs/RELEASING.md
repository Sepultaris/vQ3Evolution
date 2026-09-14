# Preparing a Windows release

The 1.0 package is assembled from an explicit allowlist, not by copying a used
game installation. `dist/` and `build-*` stay ignored. Do not delete local game
data, SDKs, profiling evidence or rollback backups as part of source cleanup.
The inherited root `ChangeLog` describes upstream work, not VQ3 Evolution 1.0;
use [the current release notes](RELEASE_1.0.md).

## Clean build

Use a new build directory, MSYS2 UCRT64 GCC/Make, Python 3.10+, PowerShell and
the Vulkan SDK for the local post shaders. Fetch the pinned Streamline SDK using
`tools/fetch-streamline.ps1` if it is not already available. In UCRT64:

```sh
make PLATFORM=mingw64 ARCH=x86_64 BUILD_DIR=build-release-1.0 \
  STREAMLINE_DIR=build-widescreen/deps/streamline-sdk-v2.12.0 \
  USE_NVIDIA_DLSS=1 BUILD_SERVER=1 VERSION=1.0 -j6 release
```

`VERSION=1.0` suppresses the development Git suffix without changing protocols
or game-data identifiers. Makefile, the fallback version in `q_shared.h` and
Windows VERSIONINFO must agree for a new release. The 1.0 Windows file version
is `1.0.0.0`. Record warnings; do not describe a successful build as warning-free.

From PowerShell:

```powershell
tools/compile-postfx.ps1 -VulkanSDK C:/VulkanSDK/1.4.350.0 `
  -OutputDirectory build-release-1.0/release-mingw64-x86_64/postfx
python tests/repo_hygiene_check.py --shader-payloads
python tests/release_defaults_check.py
python tests/pt_chrome_check.py
git diff --check
```

The first build also creates source-built baseq3/missionpack DLLs and QVMs. They
are executable game code, not proprietary maps/textures, and belong in the
binary package but not the source repository. Checked-in embedded Vulkan
payloads remain intentional build inputs.

## Assemble and audit

Point `--msvc-crt` at the x64 CRT redistribution directory of a licensed Visual
Studio installation, not Windows/System32 or an arbitrary downloaded DLL site.
The following path is an example; use the version actually installed:

```powershell
python tools/package-release.py `
  --build build-release-1.0/release-mingw64-x86_64 `
  --output dist/VQ3Evolution-1.0-win64 `
  --msvc-crt "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT"
```

The tool refuses to replace an existing release folder or source archive.
It copies only the engine/renderers/server, six native and six QVM modules,
required production NVIDIA DLLs, source-built NR bridge, SDL, three MinGW and
three Microsoft CRT DLLs, local postfx packages and notices. NR/NRD experiment
binaries, OpenAL, game/mod content, personal settings, SDKs and captures are
excluded. It checks PE architecture and recursive imports against packaged
libraries or an explicit Windows API allowlist, never a developer's PATH.

All package files receive SHA-256 checksums. `BUILD-MANIFEST.json` records the
build command, compiler, dependency graph, SDK, source base commit and the hash
of `VQ3Evolution-1.0-source.zip`. That companion archive contains the **current
working tree** (tracked plus untracked addable sources, excluding pending
deletions), not an obsolete HEAD archive. No staging, commit, tag or push occurs.

Recheck without replacing anything:

```powershell
python tools/package-release.py --verify-only `
  --build build-release-1.0/release-mingw64-x86_64 `
  --output dist/VQ3Evolution-1.0-win64 `
  --msvc-crt "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT"
```

## Before publishing

- Run the offline regressions and a short isolated-profile test of the actual
  packaged executable/DLLs. Keep test homes and logs outside the release folder.
  Include raster, DLAA/RR and Team Arena startup; disclose skipped or failed cases.
- Verify imported DLL closure without developer PATH, production NVIDIA/MS CRT
  signatures, 1.0 version metadata, all effects and absence of game/user/test files.
- Inspect [current issues](STATUS.md). Unresolved NVIDIA lifecycle/validation and
  FG quality limitations remain release notes, not silently resolved by packaging.
- Keep [third-party notices](THIRD_PARTY.md). Confirm redistribution rights for
  supplied assets (lens dirt was confirmed by the maintainer on 2026-09-14).
- Review GPL and NVIDIA runtime distribution terms together; this packaging
  operation is not legal clearance. NVIDIA's notices are included unchanged.
  Microsoft app-local CRT files come from Visual Studio's redistributable list;
  see [Microsoft's redistribution guide](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170).
- Commit the reviewed sources, then tag/publish only when requested. Upload the
  matching source archive alongside the binary archive and keep all notices.
  If source changes after packaging, rebuild/repackage instead of reusing stale
  version-1.0 binaries. Do not publish the used development build tree.

The local source archive records uncommitted files deliberately; GitHub's
automatic source ZIP for the previous commit is not its replacement.

## Recorded 1.0 package smoke tests

The validation harness used for these checks lives in the maintainer's local
`tests/` directory and is intentionally excluded from Git and from the source
archive. Run it from a checkout where that private harness is available:

```powershell
tests/run-release-smoke.ps1 -RuntimeDirectory <package> -ContentDirectory
<licensed-game-install>
```

This keeps assets and configs outside the package, removes
developer directories from PATH and owns its game process through an independent
45-second guard. Use `-Mode pt` for DLAA/RR or `-Game missionpack` for Team Arena;
`-NativeModules` selects the source-built DLLs instead of the default QVMs.
`-OfficialDefaults -Mode pt` instead tests the complete built-in preset, including
2× FG, at 1920×1080 without test quality overrides. Compare its saved config with
`release_defaults_check.py --profile <home/baseq3/release-defaults.cfg>`.
Run with normal desktop permissions: restricted-agent results below differed
from ordinary desktop runs. Never disable the guard or call a timeout a pass.

Final-preset evidence under ignored `build-release-1.0/`:

- `smoke-baseq3-pt-61fc7530676a4ac5b01a8b09fca62d69`: official 1080p preset,
  QVM modules, RR 960×540→1920×1080, active 2× FG, normal 13.828 s exit.
  All 258 registered preset values matched; two pillar views and clean RR view
  showed chrome reflections. No source config changed.
- `smoke-baseq3-raster-cf714d1f7a924ef88b46842e95ea1ea9`: raster/QVM,
  all ten effects ready, normal 4.781 s exit.
- `smoke-missionpack-raster-b6f8f0644e6c43e09a6d9bdc127f857c`: native Team Arena,
  empty profile, audio defaults preserved, all effects ready, normal 5.547 s exit.

Initial preset attempts `037d17165be34b84a816ce348b0387dd` (logging-wrapper
abort) and `5953810953104c01b90653c0782b15ee` (invalid camera command and one
default mismatch) are not final acceptance evidence. The latter errors were
corrected and the preset lookup test now covers every entry. Team Arena attempt
`77110f1dc549481facaeb73aeedd93ac` inherited a content-directory config; empty
profile files now prevent that fallback. No test files are copied into releases.

Earlier pre-preset build evidence:

- `smoke-baseq3-raster-b2d75dc582404fb985a73c52c87a8040`: normal 4.875 s exit.
- `smoke-baseq3-pt-4c690041e5a740bc9d214531ea869fe6`: native modules, active RR,
  normal 4.953 s exit.
- `smoke-missionpack-raster-e04d5b06d1724c55a7451dfe66a9c52a`: native Team Arena,
  normal 5.484 s exit.
- Earlier restricted runs `smoke-baseq3-raster-51f7a169eb044090908c3bd1757d40c8`,
  `smoke-baseq3-pt-68387a34d98e4503bce9b877f95cf798` and
  `smoke-missionpack-raster-4e70cf34fbc24214a813df0721b118ec` required their
  45-second guards. Raster/Team Arena reached NGX shutdown; PT reached RR creation
  but not its captures. The successful retests are not a lifecycle bug fix.

The final folder is assembled from these same clean-build binaries and shaders,
with updated release documentation. Package hashes verify exact files, not
visual quality. Raw test logs/screenshots are deliberately not distributables.
