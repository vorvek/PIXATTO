# <img src="assets/icon/pixelizer-icon-32.png" width="32" height="32" alt="Pixelizer icon"> Pixelizer

[![Release](https://github.com/vorvek/Pixelizer/actions/workflows/release.yml/badge.svg)](https://github.com/vorvek/Pixelizer/actions/workflows/release.yml)

Pixelizer is a desktop image tool for turning PNG, JPG, and BMP files into pixel art. It is written in C++20, uses SDL3 and Dear ImGui, and is designed to build on Windows, Linux, and macOS.

![Pixelizer showing an original image beside a processed pixel-art result](docs/pixelizer-in-action.png)

## Features

- Side-by-side original and processed image viewports, plus a stacked layout with the result above the source image
- Draggable viewport splitter for resizing the preview panes
- Independent zoom for original and result previews
- Undo/redo for processing edits with Ctrl+Z and Ctrl+Shift+Z
- Direct numeric entry by double-clicking numeric controls
- PNG/JPG/BMP import via `stb_image`
- Drag-and-drop image import with replace confirmation
- PNG export via `stb_image_write`
- Lospec `.hex` palette import with persistent palette list
- Palette mapping or simpler per-channel color reduction
- Block pixelization with linear-light block averaging
- Optional Bayer, deterministic blue-noise, Floyd-Steinberg, Jarvis-Judice-Ninke, Atkinson, or Riemersma dithering with percentage control and selectable Bayer pattern sizes
- Brightness, contrast, gamma, levels, saturation, and tint controls

## Downloads

Tagged releases are built by GitHub Actions and published on the [Releases page](https://github.com/vorvek/Pixelizer/releases) with Windows x64, Linux x64, and macOS assets. Each package contains the executable plus the project readme and third-party license notes.

## Build From Source

Pixelizer uses CMake presets and fetches its libraries at configure time.

### Windows

```powershell
cmake --preset ninja-release
cmake --build --preset ninja-release
.\build\ninja-release\pixelizer.exe
```

### Linux

Install SDL build dependencies first. On Ubuntu:

```bash
sudo apt-get install build-essential cmake git ninja-build pkg-config \
  libasound2-dev libdbus-1-dev libdecor-0-dev libegl1-mesa-dev \
  libgl1-mesa-dev libgles2-mesa-dev libibus-1.0-dev libpipewire-0.3-dev \
  libpulse-dev libudev-dev libwayland-dev libx11-dev libxcursor-dev \
  libxext-dev libxfixes-dev libxi-dev libxkbcommon-dev libxrandr-dev \
  libxss-dev libxtst-dev wayland-protocols
cmake --preset ninja-release
cmake --build --preset ninja-release
./build/ninja-release/pixelizer
```

### macOS

```bash
brew install cmake ninja
cmake --preset ninja-release
cmake --build --preset ninja-release
./build/ninja-release/pixelizer
```

## Dependencies

The build statically links SDL3 `release-3.4.4`, Dear ImGui `v1.92.7`, and stb image libraries. See [THIRD_PARTY.md](THIRD_PARTY.md) for license notes.
