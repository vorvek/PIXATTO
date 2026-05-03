# Changelog

## 1.7.2 - 2026-05-03

- Changed the default pixel size from `8` to `2`.
- Changed PNG, raw, batch, and model texture exports to write one output pixel per pixelization block, reducing exported dimensions to `ceil(width / Pixel Size)` by `ceil(height / Pixel Size)`.

## 1.7.1 - 2026-05-02

- Added a `Close File` control to the result/model preview header so the current image or model can be closed without changing processing settings.
- Closing a model now returns PIXATTO to image mode, letting subsequent image imports and drops start normal image work instead of adding textures to the model drawer.

## 1.7.0 - 2026-05-01

- Renamed the application from Pixelizer to PIXATTO across the app title, release packages, documentation, source namespace, headers, and build targets.
- Added first-run migration for existing Pixelizer palette and preset folders into PIXATTO's preference folder.
- Added a batch pixelize workflow that queues multiple images, writes PNG or raw indexed output to a chosen folder, and can run with the current settings or a saved preset.
- Added saved processing presets, including apply, overwrite, delete, conflict handling, and localized UI text.
- Switched image import decoding to SDL_image and added TGA, GIF, WebP, JPEG XL, QOI, TIFF, and PNM-family imports with binary imported transparency.
- Extended drag-and-drop, file picker, and file command handling for the new image import formats.
- Added regression tests for preset persistence, localized preset labels, file command handling, and SDL_image import behavior.

## 1.6.1 - 2026-05-01

- Added FBX model import through ufbx and DAE model import through tinyxml2, keeping the importer on permissive MIT-compatible licensing.
- Added side-by-side original/result previews in the model texture gallery so imported model textures can be checked before assignment.

## 1.6.0 - 2026-05-01

- Added GLB/glTF and OBJ model import with an unshaded OpenGL preview.
- Added model texture processing for glTF base-color textures and OBJ diffuse textures, applying the existing pixel effects to all affected textures at once.
- Added a stacked model workspace with an original texture gallery, live processed model viewport, material list, and texture drawer for drag/drop assignment.
- Added grey unshaded fallback rendering for model materials without assigned textures.
- Added orbit, pan, zoom, reset, and origin controls for the model preview, plus a small viewport gizmo.
- Added a localized Help dialog with keyboard, mouse, import, and model texture assignment controls.
- Added sequential PNG export for processed model textures, using the source texture names as defaults.
- Switched the UI renderer to SDL3 + OpenGL3 so the ImGui workspace and model preview share the same frame.
- Added asynchronous model loading and a runtime debug log at the system temp path for diagnosing importer or renderer issues.
- Fixed model-preview framebuffer restore behavior that could leave the UI black after rendering a model.

## 1.5.0 - 2026-05-01

- Added False Floyd Steinberg, Filter Lite, Zhigang Fan, Shiau-Fan, Stucki, Burkes, Sierra, Two Row Sierra, clustered-dot, horizontal, vertical, and diagonal dithering options.
- Added the Lospec `nintendo-entertainment-system` and `carnival-32` palettes to the bundled defaults.
- Replaced the CGA default palettes with local `1-bit-greyscale`, `2-bit-greyscale`, `3-bit-greyscale`, and `4-bit-greyscale` palettes, with a one-time cleanup for unchanged seeded CGA files.
- Reworked preview processing for very small pixel sizes, especially `1x` and `2x`, to avoid building and rereading a full block grid when each block can be written directly.
- Added a direct `1x` path that uses precomputed per-channel reduction tables for the common no-palette/no-diffusion preview case.
- Parallelized large direct block renders across block rows, capped to a small worker count so bigger images update faster without overwhelming the UI.
- Cached the small set of block weight kernels up front and added a uniform-kernel shortcut, reducing inner-loop work for tiny weighted blocks such as `2x2`.
- Optimized Riemersma dithering by replacing the 16-entry per-pixel error-history scan with an equivalent rolling weighted error.
- Added a cached 6-bit palette lookup for large-palette diffusion modes so repeated previews with a 256-color palette avoid scanning every palette entry for every pixel.
- Improved the worst-case `1672x941`, `1x`, 256-color palette, 50% Riemersma path from roughly `690 ms` in the UI to around `170 ms` in the core processing benchmark after the palette lookup is warm.
- Added an `Export...` menu with PNG and raw indexed export options.
- Added raw export as `.raw` palette indices, shared `.pal` palette sidecars, and optional `.msk` 1-bit transparency masks.
- Changed PNG export to emit indexed 8-bit PNGs when possible, falling back to truecolor PNGs only when the result needs it.
- Increased the language picker height and list sizing so all language options fit without a tiny scrollbar.
- Added image-processing regression tests for the new fast paths, transparency behavior, palette-constrained Riemersma output, and `1x` equivalence with the block path.

## 1.3.1 - 2026-04-30

- Made native file picker dialogs behave modally in the app by disabling controls, drops, and shortcuts until the picker closes.
- Added a status message when the platform file picker cannot be opened.

## 1.3.0 - 2026-04-30

- Added preliminary transparency support (single bit).

## 1.2.2 - 2026-04-29

- Added a localized About dialog with project, MIT license, dependency, palette, and asset credits.

## 1.2.1 - 2026-04-28

- Added 21 bundled Lospec `.hex` palettes, seeded as normal saved palettes on first launch so they can still be deleted.
- Added Lospec palette source attribution and original palette page links to the third-party credits.
- Copied bundled assets beside local build outputs so default palettes are available from source builds as well as packaged releases.

## 1.2.0 - 2026-04-28

- Added a language picker with flag icons and localized UI labels for European languages, Simplified and Traditional Chinese, Korean, and Japanese.
- Added Unicode font fallback loading so localized labels can render across Latin, Greek, Cyrillic, CJK, Korean, and Japanese scripts.
- Kept ImGui widget IDs stable across language changes so controls and popups do not lose state when switching languages.

## 1.1.4 - 2026-04-28

- Fixed manual numeric entry so clicking Apply parses and uses the typed value before closing the dialog.

## 1.1.3 - 2026-04-28

- Improved preview update performance by caching adjustment and color-space lookup data instead of recomputing it for every source pixel.
- Reused weighted block kernels across blocks so spatial weighting work is not repeated for every pixel.
- Tightened block read and write loops with row-pointer iteration to reduce indexing overhead.
- Reduced generated-palette overhead by rebuilding merged quantized color buckets once after aggregation.
