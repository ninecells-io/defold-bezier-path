# Bezier Path

Inspired by Apples UIKit UIBezierPath class, this extension for the [Defold](https://defold.com/) game engine provides bezier path to render textures.

## Key Features

* Platform agnostic - no dependencies to OS functions
* Define bezier path by linear points, quad curve point, or arc points
* Rendering with anti aliasing
* Fill bezier paths with solid color aswell as linear or shaped gradients
* Draw stroke around bezier path
* Strokes with selectable position, line caps, and line joins
* Add inner or outer drop shadows
* Add alpha masks

## Scoped paths and cropped textures

Paths use stable numeric handles. Deleting one path does not change any other
handle. Short-lived paths should use `with_path`, which releases the native
path even when the drawing callback raises an error:

```lua
local pixels = bezierpath.draw_texture(128, 128, function(buffer_info)
    bezierpath.with_path(buffer_info, function(handle)
        bezierpath.rounded_rect(handle, 8, 8, 112, 112, 16)
        bezierpath.fill_path(buffer_info, color)
    end)
end)
```

`get_path_bounds(handle, padding)` returns an `{ x, y, width, height }` table.
`draw_texture_region(bounds, draw_fn, params)` renders board-coordinate paths
into a factor-1 cropped RGBA buffer and returns `pixels, normalized_bounds`.
`create_texture_region(name, bounds, draw_fn, params)` creates the equivalent
dynamic GUI texture and returns `name, normalized_bounds`.

Low-level `buffer_info` tables may include `origin_x` and `origin_y`; both
default to zero. The origin maps a cropped buffer back into the coordinate
space in which its path was constructed.

## Native performance benchmark

The permanent benchmark covers 64, 256, and 512-pixel fills, gradients,
shadows, strokes, and glow plus a representative Classic Sudoku board:

```sh
make -C benchmark run
```

See `benchmark/README.md` for the release-build baseline and acceptance gates.
