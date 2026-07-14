# mp

`mp` is a small desktop music player written in C99. It uses miniaudio for
decoding and playback, raylib for the interface, and NanoSVG for scalable UI
icons.

## Features

- MP3, FLAC, and WAV playback
- drag-and-drop playlists
- title, artist, and album metadata
- seeking, volume control, and mute
- repeat all, repeat one, and shuffle without repeats within a cycle
- M3U and M3U8 playlist loading and saving
- resizable interface with Cyrillic metadata support

## Requirements

The current build targets Linux with X11 and OpenGL. A C99 compiler and the
development libraries for OpenGL and X11 are required. raylib, miniaudio,
NanoSVG, and the build helper are vendored in `thirdparty/`.

## Build

Bootstrap the build tool after cloning:

```sh
cc -std=c99 -o nob nob.c
```

Build the default dynamically linked raylib configuration:

```sh
./nob
```

Build with raylib linked statically:

```sh
./nob static
```

The executable is written to `build/mp`. The static configuration still uses
the system OpenGL, X11, and C runtime libraries.

## Run

Build and start an empty player:

```sh
./nob run
```

Open audio files or an M3U playlist immediately:

```sh
./nob run music/first.flac music/second.mp3
./nob run playlists/favorites.m3u
```

Use `./nob run static [files...]` to run the static raylib configuration.
Audio files and M3U playlists can also be dropped onto the window. Relative
entries in an M3U file are resolved from that playlist's directory.

`Ctrl+S` saves the current playlist. When the player was opened with one M3U
file, that file is updated atomically. Otherwise the playlist is written to
`playlist.m3u` in the current working directory.

## Controls

| Input | Action |
| --- | --- |
| `Space` | Play or pause |
| `Left` / `Right` | Previous or next track |
| `Up` / `Down` | Increase or decrease volume |
| `M` | Toggle mute |
| `Delete` | Remove the current track |
| `Ctrl+Delete` | Clear the playlist |
| `Ctrl+S` | Save the playlist |
| Mouse wheel | Adjust volume or scroll the playlist |
| Progress bar | Seek within the current track |

Repeat, shuffle, playlist visibility, and track selection are available from
the interface.

## Tests

Run the core playback-order, playlist, M3U, and metadata checks with:

```sh
./nob test
```
