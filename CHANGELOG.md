# Changelog

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
