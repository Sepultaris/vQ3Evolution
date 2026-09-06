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
- Build the affected renderer and native/QVM modules.
- Test both the main menu and a loaded map for graphics or UI changes.
- Document user-visible settings, requirements, and fallback behavior.
- Include relevant logs or screenshots in the issue or change description, not
  in the source tree.

Do not commit build directories, Quake III PK3 data, local configuration files,
downloaded SDKs, NVIDIA runtime DLLs, test homes, crash dumps, or generated logs.
The root `.gitignore` covers the standard locations.

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
