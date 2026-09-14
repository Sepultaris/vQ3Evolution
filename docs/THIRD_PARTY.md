# Third-party software and release assets

VQ3 Evolution derives from id Software's Quake III source, ioquake3 and vkQuake3.
The engine's GPL terms are in [COPYING.txt](../COPYING.txt); upstream context is
in [id-readme.txt](../id-readme.txt). Component-specific notices remain in source.

## Release payload

- SDL 2: Sam Lantinga and contributors, zlib license; bundled `SDL264.dll`
  comes from `code/libs/win64`. Its source notice is in `code/SDL2/include/SDL.h`.
- zlib/minizip: Jean-loup Gailly, Mark Adler and Gilles Vollant; zlib-style terms.
- JPEG: this software is based in part on the work of the Independent JPEG Group.
  The complete IJG notice is in `code/jpeg-8c/README`.
- Ogg 1.3.3, Vorbis 1.3.6 and Opusfile 0.9: Xiph.Org Foundation/contributors,
  BSD-style terms. Original versioned notices are preserved in
  [Ogg](licenses/ogg.txt), [Vorbis](licenses/vorbis.txt) and
  [Opusfile](licenses/opusfile.txt). Sources:
  [Ogg v1.3.3](https://github.com/xiph/ogg/blob/v1.3.3/COPYING),
  [Vorbis v1.3.6](https://github.com/xiph/vorbis/blob/v1.3.6/COPYING),
  [Opusfile v0.9](https://github.com/xiph/opusfile/blob/v0.9/COPYING).
- Opus: Xiph.Org Foundation, Skype Limited and contributors. The original
  [Opus 1.2.1 notice](licenses/opus.txt), plus the SILK source notice, accompany
  the release; source: [Opus v1.2.1](https://github.com/xiph/opus/blob/v1.2.1/COPYING).
- GCC runtime/libstdc++ and MinGW winpthreads: their installed license texts,
  including the GCC Runtime Library Exception, accompany the copied runtime DLLs.
- Microsoft Visual C++ runtime: app-local `msvcp140.dll`, `vcruntime140.dll`
  and `vcruntime140_1.dll` are copied unchanged from Visual Studio's x64 CRT
  redistributable directory, not from the OS. They remain Microsoft software
  under the applicable Visual Studio distribution terms, not GPL engine code.
  See [Microsoft's redistribution documentation](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170).
- Vulkan headers: Khronos Group, Apache-2.0; bundled header notices and the
  [Apache license](licenses/Apache-2.0.txt) are preserved.
- NVIDIA Streamline 2.12.0 production runtime and DLSS/DLAA/RR/FG/Reflex binaries:
  NVIDIA and contributors. Include Streamline's license/third-party notices and
  the SDK's DLSS and Reflex license files. This software contains source code
  provided by NVIDIA Corporation. NVIDIA technology names identify optional
  integrations, not NVIDIA sponsorship or endorsement of this source port.
- The source-built `nvngx.dll` bridge is not NVIDIA's Neural Rendering runtime.
  See the [integration notice](DLSSNR-THIRD-PARTY-NOTICE.txt). The game-sourced
  `nvngx_dlssnr.dll` and local experimental `vq3e_nrd.dll` are excluded.

## Post effects

The package's GLSL is source-distributed with the engine. The bokeh implementation
records its visual/feature reference to Martins Upitis' DoF with bokeh v2.4 in
`postfx/bokehdof.frag`; it is independently implemented, not a ReShade shader.

`postfx/lensdirt.png` was supplied by the project maintainer. On 2026-09-14 the
maintainer explicitly confirmed permission to redistribute that image in this
release. This confirmation is not a claim that the image is public domain or
that unrelated third-party rights have been independently audited.

## Publication review

The packager retains notices; it does not grant new permissions or certify legal
compatibility. NVIDIA's separate runtime terms and GPL distribution obligations
must both be reviewed before publishing. In particular, do not assume that
dynamic loading alone settles license compatibility. Keep the matching source
archive available alongside binaries. No proprietary Quake III or mod content
may be included simply because it was present in a development installation.
