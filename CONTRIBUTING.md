# Contributing to VQ3 Evolution

Contributions should preserve Quake III compatibility while keeping new engine
features source-native and maintainable.

## Before making a change

- Create a focused branch from `main`.
- Search the existing source and documentation before introducing a new cvar,
  menu control, renderer path, or compatibility layer.
- Keep `baseq3`, protocol values, game-data formats, and mod-facing interfaces
  stable unless an intentional compatibility break has been agreed upon.

## Submitting changes

- Keep each commit focused and describe the behavior it changes.
- Run the [repository and offline checks](docs/TESTING.md) before staging;
  review untracked files as well as the tracked diff.
- Build the affected renderer and native/QVM modules.
- Test both the main menu and a loaded map for graphics or UI changes.
- Document user-visible settings, requirements, and fallback behavior.
- Include relevant logs or screenshots in the issue or change description, not
  in the source tree.

Do not commit build directories, Quake III PK3 data, local configuration files,
downloaded SDKs, NVIDIA runtime DLLs, compiled/extracted QVMs, scratch inspection
directories such as `.build-tmp`, test homes, crash dumps, or generated logs.
The root `.gitignore` covers the standard locations.

Embedded GLSL bytecode/C arrays under `code/renderer_vulkan/shaders/Compiled`
and the generated blue-noise header are intentional source-build inputs.
Regenerate and include them with the source change; do not ignore or delete
them as old build output. GitHub marks these generated files for collapsed
review, but they still belong in the commit. Already-tracked upstream libraries
retain their existing provenance; adding any new binary dependency needs an
explicit review.

Keep current setup/defaults in the feature guides and [status](docs/STATUS.md).
Move superseded implementation narratives to `docs/archive` with their original
test settings and caveats. Do not erase failed experiments or present historical
one-scene results as current whole-renderer performance. Never commit credentials
or include full local profiles/captures just to support a benchmark claim.

## Reporting problems

Use the repository's issue tracker when one is configured. Include:

- Exact reproduction steps and whether the issue occurs in a loaded map
- Renderer, resolution, fullscreen/window mode, and relevant cvar values
- GPU, driver, and operating-system versions for renderer issues
- The last useful section of the console or engine log
- Whether the issue reproduces with unmodified `baseq3` data

## Licensing

Contributions to GPL-covered engine code must be compatible with GNU GPL v2.
Do not submit proprietary game data or third-party runtime binaries. Preserve
required copyright and third-party notices.
