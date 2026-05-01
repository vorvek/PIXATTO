# Third-Party Dependencies

Pixatto fetches these dependencies at configure time and links them into the desktop executable:

- SDL3 `release-3.4.4` from `libsdl-org/SDL`, under the zlib license.
- SDL_image `release-3.4.2` from `libsdl-org/SDL_image`, under the zlib license.
- Dear ImGui `v1.92.7` from `ocornut/imgui`, under the MIT license.
- stb at commit `31c1ad37456438565541f4919958214b6e762fb4` from `nothings/stb`, using its public-domain/MIT dual license.
- tinygltf `v2.9.7` from `syoyo/tinygltf`, under the MIT license.
- tinyobjloader `v2.0.0rc13` from `tinyobjloader/tinyobjloader`, under the MIT license.
- ufbx `v0.21.3` from `ufbx/ufbx`, using its MIT license option.
- tinyxml2 `11.0.0` from `leethomason/tinyxml2`, under the zlib license.

SDL_image is configured with vendored permissive codec libraries for extended imports:

- libwebp `1.3.2-SDL`, under its BSD-style license.
- libjxl `v0.7.3-SDL`, under its BSD 3-Clause license.
- Brotli from the vendored libjxl tree, under the MIT license.
- Highway from the vendored libjxl tree, under the Apache 2.0 license.
- libtiff `v4.7.1-SDL`, under its BSD-style license.

Pixatto bundles these default palette files from Lospec. Links point to the original palette pages:

- [pico-8](https://lospec.com/palette-list/pico-8)
- [dawnbringer-16](https://lospec.com/palette-list/dawnbringer-16)
- [dawnbringer-32](https://lospec.com/palette-list/dawnbringer-32)
- [shmupy-16](https://lospec.com/palette-list/shmupy-16)
- [aurora](https://lospec.com/palette-list/aurora)
- [carnival-32](https://lospec.com/palette-list/carnival-32)
- [db-iso22](https://lospec.com/palette-list/db-iso22)
- [amiga-pixels-64](https://lospec.com/palette-list/amiga-pixels-64)
- [2bit-demichrome](https://lospec.com/palette-list/2bit-demichrome)
- [windows-95-256-colours](https://lospec.com/palette-list/windows-95-256-colours)
- [microsoft-windows](https://lospec.com/palette-list/microsoft-windows)
- [commodore64](https://lospec.com/palette-list/commodore64)
- [commodore-vic-20](https://lospec.com/palette-list/commodore-vic-20)
- [msx](https://lospec.com/palette-list/msx)
- [nintendo-entertainment-system](https://lospec.com/palette-list/nintendo-entertainment-system)
- [amstrad-cpc](https://lospec.com/palette-list/amstrad-cpc)
- [apple-ii](https://lospec.com/palette-list/apple-ii)

The README screenshot and icon files are original project assets.
