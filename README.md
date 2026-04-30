# <img src="assets/icon/pixelizer-icon-32.png" width="32" height="32" alt="Pixelizer icon"> Pixelizer

[![Release](https://github.com/vorvek/Pixelizer/actions/workflows/release.yml/badge.svg)](https://github.com/vorvek/Pixelizer/actions/workflows/release.yml)

Pixelizer is a desktop image tool for turning PNG, JPG, and BMP files into pseudo-pixel art. It runs on Windows, Linux, and macOS, and is written in C++20 with SDL3 and Dear ImGui.

![Pixelizer showing an original image beside a processed pseudo-pixel-art result](docs/pixelizer-in-action.png)

## Features

- Default single result viewport, with optional side-by-side or stacked original/result viewports
- Draggable viewport splitter for resizing the preview panes
- Independent zoom for original and result previews
- Undo/redo for processing edits with Ctrl+Z and Ctrl+Shift+Z
- Direct numeric entry by double-clicking numeric controls
- PNG/JPG/BMP import via `stb_image`
- Drag-and-drop image import with replace confirmation, plus `.hex` palette drop import
- PNG export that writes indexed 8-bit PNGs when the result fits in 256 palette entries, and truecolor PNGs otherwise
- Raw indexed export with shared palette sidecars and optional 1-bit transparency masks
- Validated Lospec `.hex` palette import with duplicate handling, plus in-app palette creation, color editing, color deletion, save, save-new, and palette deletion
- Bundled default palettes, including Lospec palettes and greyscale ramps, that are seeded into the palette library on first launch and can still be deleted
- Palettes are limited to 256 colors, whether imported or created in the app
- Palette mapping or simpler per-channel color reduction
- Preliminary single-bit transparency preservation with a special palette entry
- Block pixelization with linear-light block averaging
- Optional ordered, blue-noise, error-diffusion, and Riemersma dithering with percentage control and selectable Bayer pattern sizes
- Brightness, contrast, gamma, levels, saturation, and tint controls

## Downloads

Download the latest release from the [Releases page](https://github.com/vorvek/Pixelizer/releases).

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

## Raw Export Format

Raw export writes a headerless 8-bit indexed image plus sidecar files:

- `<image>.raw` contains exactly `width * height` bytes in row-major order, left-to-right and top-to-bottom. Each byte is a palette index from `0` to `255`.
- `<palette>.pal` contains the shared palette. If a saved palette is active, the sidecar is named from that palette, so multiple raw images can share one palette file. If there is no saved active palette, it uses the exported image stem. The file is ASCII text with one uppercase `RRGGBB` color per line; the line number is the palette index.
- `<image>.msk` is written only when the result contains transparent pixels. It contains `ceil(width * height / 8)` bytes in the same pixel order. Bits are packed most-significant-bit first within each byte. A `1` bit means transparent; a `0` bit means opaque. For transparent pixels, the corresponding `.raw` index byte is `0` and should be ignored.

Pixelizer raw export supports up to 256 opaque palette colors plus binary transparency. It does not store width or height in the files, so projects should keep dimensions in their own asset metadata.

For pixel `i = y * width + x`:

```c
uint8_t index = raw[i];
bool transparent = mask && (mask[i / 8] & (0x80 >> (i & 7)));
```

## Dependencies

The build statically links SDL3 `release-3.4.4`, Dear ImGui `v1.92.7`, and stb image libraries. See [THIRD_PARTY.md](THIRD_PARTY.md) for license notes.

## License

Pixelizer is released under the [MIT License](LICENSE).
