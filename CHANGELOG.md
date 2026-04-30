# Changelog

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
