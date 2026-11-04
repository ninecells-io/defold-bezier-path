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

## Texture supersampling

Generated textures default to 2x supersampling. Applications with density-independent GUI roots can match the texture resolution to the effective display scale:

```lua
local bezierpath = require("bezierpath/bezierpath")
local factor = math.max(2, window.get_display_scale() * interface_scale)
bezierpath.set_default_texture_factor(factor)
```

`get_default_texture_factor()` returns the current default. The setter clamps values below 2 to 2 and falls back to 2 for invalid numbers. An explicit factor passed to `create_texture()` still overrides it, and the factor remains part of the texture cache identity.

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
`create_texture_region(key, bounds, draw_fn, params)` creates the equivalent
dynamic GUI texture and returns `texture_id, normalized_bounds`.

## Optional persistent texture cache

High-level textures can be cached as validated RGBA files in Defold's portable
application save location. Caching is disabled unless the consumer configures it
before creating GUI textures:

```lua
local bezierpath = require("bezierpath/bezierpath")

bezierpath.configure_texture_cache({
    enabled = true,
    folder = "my-game-bezier-textures",
    version = sys.get_config_string("project.version", "dev") .. ":1",
    max_size_bytes = 256 * 1024 * 1024, -- Optional platform-default override.
})
```

`folder` is passed to `sys.get_save_file()` as the application-specific save
namespace. `version` belongs to the consumer; changing it removes the files
tracked by the previous cache manifest and causes textures to be drawn again.
`bezierpath.clear_texture_cache()` performs the same invalidation immediately.

`max_size_bytes` limits the total size of tracked RGBA files, including their
headers. It defaults to 256 MiB on iOS/Android and 512 MiB elsewhere. A value of
zero prevents disk caching. Least recently used files are deleted before a new
file is written; oversized textures render without being cached. Successful
loads, stores, and same-scene GUI texture reuse refresh a persisted access order.
Configuring a smaller limit immediately trims existing files. If an eviction
fails, new writes are skipped until enough space can be reclaimed. The manifest
itself is outside the texture byte budget. Upgrading from the old unbounded
cache format clears its tracked files once.

The first argument to `create_texture`, `create_texture_region`, and
`add_gui_box` is a stable logical cache key. It must include every consumer
input that changes the resulting pixels. It is not a GUI texture name:
bezier-path hashes the key together with the cache version, texture kind,
dimensions, and scale factor, and returns an opaque runtime texture id. Owners
that explicitly delete dynamic textures must retain and delete that returned
id. `add_gui_box` returns `node, texture_id` for this purpose.

On a cache hit, the drawing callback is skipped. Missing, corrupt, truncated,
or unwritable cache files are treated as misses, so texture generation and GUI
rendering continue normally. `draw_texture` and other low-level byte-buffer
operations remain uncached.

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

## Local native-extension development

The repository includes a macOS-only foreground build pipeline for the four
sibling first-party projects. It runs the engine-independent regression tests,
serves the `defold-bezier-path`, `defold-controls`, `defold-sudoku-playboard`,
and `defold-helpers` working trees as local Defold library archives, then builds
consumers through a self-hosted Defold Extender.

### Pinned toolchain

The development toolchain is deliberately pinned in `scripts/config.sh`:

- Defold 1.13.0 (`f735c12192bf95684e6ae1ae27c400b8170fc6d8`)
- Extender `extender-v2.12.4`
- Xcode, macOS SDK, and iOS SDK 26.2
- Apple Silicon macOS builds via `http://127.0.0.1:9010`
- Local Defold dependency archives via `http://127.0.0.1:9012`

Install Xcode 26.2 as `/Applications/Xcode_26.2.app`. It can coexist with a
newer `/Applications/Xcode.app`; the setup does not change the system-wide
`xcode-select` value. Accept the Xcode license and launch that Xcode once so it
can install any required components.

Provision the local builder with:

```sh
make setup-check
make setup-local-extender
```

Setup clones the pinned Defold and Extender sources, downloads the matching
Bob, packages the local Apple SDKs, builds the Extender JARs, and installs the
standalone environment below `.local-build/`. Expect several minutes of setup
time and several gigabytes of disk usage. Apple SDK packages never leave the
machine and `.local-build/` is ignored by Git. The setup needs network access
for Git, Gradle, Zig, .NET, and Homebrew packages. It also installs Homebrew
`openjdk@25` when a full JDK is unavailable. Defold's embedded Java 25 runtime
is sufficient for Bob but intentionally omits `java.instrument` and cannot run
Gradle's instrumentation agent.

### Everyday workflow

Start both local services from this repository:

```sh
make extender-start
```

The Extender lifecycle commands manage both the native build server and the
companion dependency archive server. The normal `one-sudoku` project is pinned
to the four localhost archives and its shared Editor settings select the local
Extender. In Defold, use **Project → Fetch Libraries** after changing an
exported package file.

Start the foreground watcher from this repository:

```sh
make watch
```

The command starts both services if needed, performs an initial build of all
three consumers, and watches every exported file in all four first-party
packages. Saves are debounced. If files change during a build, one follow-up
build is queued. Test or service failures are printed but do not terminate the
watcher; fix the source and save again. Stop watching with Ctrl-C. The services
stay available for later builds and can be managed explicitly:

```sh
make extender-status
make extender-start
make extender-stop
```

Individual pipeline stages are also available:

```sh
make test       # native regression and pipeline unit tests
make stage-consumers # refresh all isolated consumer staging roots
make stage-app       # refresh only the isolated one-sudoku staging root
make dev-build       # test, stage, and build all consumers through local Extender
```

Use `CONSUMERS` to stage, build, or watch a subset without changing the default:

```sh
make dev-build CONSUMERS="defold-controls"
make stage-consumers CONSUMERS="defold-sudoku-playboard one-sudoku"
make watch CONSUMERS="one-sudoku"
```

The accepted identifiers are `defold-controls`, `defold-sudoku-playboard`, and
`one-sudoku`. Their sibling checkout locations can be overridden with
`DEFOLD_CONTROLS_ROOT`, `DEFOLD_SUDOKU_PLAYBOARD_ROOT`, and `ONE_SUDOKU_ROOT`.
Build artifacts and staged projects are stored below
`.local-build/<consumer>-stage/`. Each development build is isolated: all
first-party dependency URLs in the staged `game.project` are normalized to the
local archive server, while third-party URLs remain unchanged. The local
Extender compiles the bezier-path C++ extension. No source consumer project is
modified by staging.

Resolved dependency archives from each consumer are copied into its staging
cache when available. Private dependency URLs use the app's existing
`__GITHUB_TOKEN__` convention. Store the token in the ignored repository-local
`.env` file:

```sh
GITHUB_TOKEN=github_pat_your_token
```

`GITHUB_TOKEN` or `GH_TOKEN` from the process environment takes precedence over
`.env`. The file is parsed as data and is never shell-sourced. During a build,
the token is written only to a mode-0600 temporary Bob settings file, filtered
from Bob output, and deleted afterward; source project files are never modified
or copied into staging roots.

### Hosted production build

Production uses the real `one-sudoku` sources plus
`scripts/production.dependencies.settings`. The settings override the four
localhost development URLs with pinned GitHub releases, and the build
explicitly uses Defold's hosted build service. The production command validates
all pins, renders private dependency authentication into a temporary mode-0600
settings file, and never modifies the source project. It downloads pinned Bob
automatically and does not require Xcode 26.2 or the local Extender:

```sh
make production-build
PRODUCTION_PLATFORM=arm64-ios make production-build
```

`make setup-bob` is available when only the pinned command-line builder should
be downloaded and verified.

The default is an `arm64-macos` release archive. `PRODUCTION_PLATFORM` and
`PRODUCTION_VARIANT` can select another hosted target and variant.

### Troubleshooting and cleanup

- Extender logs: `.local-build/extender-extender-v2.12.4/server/app/logs/`
- Dependency server log: `.local-build/dependency-server.log`
- Temporary SDK package server log: `.local-build/package-server.log`
- Staged consumers: `.local-build/defold-controls-stage/`,
  `.local-build/defold-sudoku-playboard-stage/`, and
  `.local-build/one-sudoku-stage/`
- A failed `make setup-check` makes no downloads or system changes.
- If setup reports a missing `libinstrument.dylib`, rerun it after this
  pipeline update; `make setup-local-extender` will select or install the full
  Homebrew OpenJDK 25 instead of Defold's trimmed runtime.
- If setup was interrupted, rerun it. Existing pinned clones, Bob, SDK
  archives, and installed components are reused.
- To fully reprovision, stop Extender and manually remove `.local-build/`.
  This is intentionally not automated because the directory contains the
  expensive packaged Xcode SDK and toolchain.

The local server setup follows Defold's standalone Extender and SDK packaging
documentation:

- https://github.com/defold/extender#running-as-a-stand-alone-server-on-macos
- https://github.com/defold/defold/tree/dev/scripts/package
