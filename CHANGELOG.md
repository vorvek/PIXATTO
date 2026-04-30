# Changelog

## 1.4.0 - 2026-04-30

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
