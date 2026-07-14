# raylib 6.0

This directory contains prebuilt raylib libraries for x86-64 Linux with X11
and OpenGL 3.3:

- `lib/libraylib.so` for the default dynamic build;
- `lib/libraylib.a` for `./nob static`;
- `include/raylib.h` for application compilation.

The retained `src/` tree is the minimal source set needed to rebuild those two
libraries offline. It enables shapes, textures, text, JPEG images, and the GLFW
X11 backend; the audio and 3D model modules are disabled. The project's
Cyrillic default-font extension lives in `src/rtext_cyrillic.h` and is wired
into `src/rtext.c`.

After changing the built-in glyph data, including a future CJK table, rebuild
and strip both libraries with:

```sh
./nob raylib
```

Run `./nob` or `./nob static` afterward to relink the player.

raylib is distributed under the zlib/libpng license in `LICENSE`. The bundled
GLFW source retains its license in `src/external/glfw/LICENSE.md`.
