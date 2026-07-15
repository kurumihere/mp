# raylib 6.0

This directory contains the source code of the official raylib 6.0 release.
`nob` builds a static library in `build/cache/` for the current operating
system; generated raylib libraries are not committed.

The complete desktop backends are retained so the same source tree can be
built natively on Linux, Windows, and macOS. Audio and 3D model modules are
disabled because mp uses miniaudio and only the 2D raylib API.

`src/rtext_cyrillic.h` and its include in `src/rtext.c` are the only local
source changes. They extend raylib's default font with Russian glyphs.

To update raylib:

1. Download a tagged source release from <https://github.com/raysan5/raylib>.
2. Replace this directory with that release while preserving
   `src/rtext_cyrillic.h` and its include in `src/rtext.c`.
3. Update the version in this file.
4. Run `./nob raylib`, `./nob`, and a second `./nob`.

The raylib part of cross-platform builds is native: bootstrap and run `nob` on
the target operating system. Linux requires OpenGL and X11 development files,
Windows requires a MinGW-w64 environment, and macOS requires the Xcode
command-line tools. Complete mp builds must still be verified on each target;
platform-specific application code is outside this vendored dependency.

raylib is distributed under the zlib/libpng license in `LICENSE`. Bundled GLFW
retains its license in `src/external/glfw/LICENSE.md`.
