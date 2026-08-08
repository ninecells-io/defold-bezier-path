# Pre-optimization fixture manifest

`pre_optimization.sha256` records the 64x64 RGBA fixtures captured from the
previous ARM64 `-O2` static library. The comparison set exercises rectangle and
arc fills, linear and shape gradients, outer shadow, open and closed strokes,
and glow.

The optimized release comparison is:

| Fixture | Changed channels | Max delta | RMSE |
|---|---:|---:|---:|
| fill circle | 0 | 0 | 0 |
| fill rectangle | 0 | 0 | 0 |
| linear-gradient circle | 0 | 0 | 0 |
| linear-gradient rectangle | 0 | 0 | 0 |
| shadow circle | 0 | 0 | 0 |
| open stroke | 0 | 0 | 0 |
| closed stroke | 0 | 0 | 0 |
| glow circle | 0 | 0 | 0 |
| shape-gradient circle | 16 / 16384 | 1 | 0.0312 |
| shape-gradient clipped polygon | 15 / 16384 | 1 | 0.0303 |
| shape-gradient concave polygon | 82 / 16384 | 1 | 0.0707 |
| shape-gradient quadratic path | 77 / 16384 | 1 | 0.0686 |

The native regression executable adds stable-handle, cache invalidation,
cropped/full equivalence, off-canvas, empty/degenerate path, zero-radius, RGB,
RGBA, and scratch-memory checks without requiring the legacy binary.
