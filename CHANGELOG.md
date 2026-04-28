# Changelog

## 1.1.3 - 2026-04-28

- Improved preview update performance by caching adjustment and color-space lookup data instead of recomputing it for every source pixel.
- Reused weighted block kernels across blocks so spatial weighting work is not repeated for every pixel.
- Tightened block read and write loops with row-pointer iteration to reduce indexing overhead.
- Reduced generated-palette overhead by rebuilding merged quantized color buckets once after aggregation.
