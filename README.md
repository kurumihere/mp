# mp

<p align="center">
  <img src="./assets/icon.png" width="50%" alt="mp logo">
</p>

<p align="center">
  <a href="https://github.com/kurumihere/mp/actions/workflows/build.yml">
    <img src="https://github.com/kurumihere/mp/actions/workflows/build.yml/badge.svg?branch=master" alt="Build">
  </a>

  <a href="https://github.com/kurumihere/mp/releases/latest">
    <img src="https://img.shields.io/github/v/release/kurumihere/mp?label=release" alt="Latest release">
  </a>
</p>

mp it's a simple, faster, and usefull music player.

## supported audio formats

- flac
- mp3
- wav

## download mp!

- Windows: [latest-installer](https://github.com/kurumihere/mp/releases/download/v0.2.0/mp-0.2.0-windows-x86_64-setup.exe), [archive](https://github.com/kurumihere/mp/releases/download/v0.2.0/mp-0.2.0-windows-x86_64.zip).
- Linux: [AppImage](https://github.com/kurumihere/mp/releases/download/v0.2.0/mp-0.2.0-linux-x86_64.AppImage), [Debian package](https://github.com/kurumihere/mp/releases/download/v0.2.0/mp-0.2.0-linux-x86_64.deb), [RPM](https://github.com/kurumihere/mp/releases/download/v0.2.0/mp-0.2.0-linux-x86_64.rpm), [Arch linux AUR](https://aur.archlinux.org/packages/mp-player-bin), [archive](https://github.com/kurumihere/mp/releases/download/v0.2.0/mp-0.2.0-linux-x86_64.tar.gz).
- MacOS: [DMG](https://github.com/kurumihere/mp/releases/download/v0.2.0/mp-0.2.0-macos-arm64.dmg), [archive](https://github.com/kurumihere/mp/releases/download/v0.2.0/mp-0.2.0-macos-arm64.tar.gz).

## build from source

### Arch linux

```sh
sudo pacman -Syu --needed \
    base-devel \
    git \
    pkgconf \
    freetype2 \
    glib2 \
    mesa \
    libglvnd \
    libx11 \
    libxcursor \
    libxi \
    libxinerama \
    libxrandr
```

### Debian / Ubuntu

```sh
sudo apt update

sudo apt install --no-install-recommends \
    build-essential \
    git \
    pkg-config \
    libfreetype6-dev \
    libglib2.0-dev \
    libgl1-mesa-dev \
    libx11-dev \
    libxcursor-dev \
    libxi-dev \
    libxinerama-dev \
    libxrandr-dev
```

### Fedora

```sh
sudo dnf install \
    gcc \
    git \
    pkgconf-pkg-config \
    freetype-devel \
    glib2-devel \
    mesa-libGL-devel \
    libX11-devel \
    libXcursor-devel \
    libXi-devel \
    libXinerama-devel \
    libXrandr-devel
```

### Windows

install [MSYS2](https://www.msys2.org/), then open the **MSYS2 UCRT64** terminal.

update MSYS2:

```sh
pacman -Syu
```

restart the UCRT64 terminal if requested, then install the dependencies:

```sh
pacman -S --needed \
    git \
    mingw-w64-ucrt-x86_64-gcc \
    mingw-w64-ucrt-x86_64-freetype \
    mingw-w64-ucrt-x86_64-glib2 \
    mingw-w64-ucrt-x86_64-pkgconf
```

> Use the **UCRT64** terminal. Do not build the project from the plain MSYS terminal, PowerShell or Command Prompt.

### macOS

install the Xcode Command Line Tools:

```sh
xcode-select --install
```

install [Homebrew](https://brew.sh/), then install the required libraries:

```sh
brew install \
    freetype \
    glib \
    pkgconf 
```

after installing the dependencies:

```sh
git clone https://github.com/kurumihere/mp.git
cd mp

cc -std=c99 -o nob nob.c
./nob
```

The resulting executable will be located at:

| Platform | Executable     |
| -------- | -------------- |
| Linux    | `build/mp`     |
| Windows  | `build/mp.exe` |
| macOS    | `build/mp`     |

build and run the application:

```sh
./nob run
```
