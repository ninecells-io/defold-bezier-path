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

## Local native-extension development

The repository includes a macOS-only foreground build pipeline for native C++
work. It runs the engine-independent regression tests first, then builds the
sibling `one-sudoku` app against this checkout through a self-hosted Defold
Extender. The real app project remains pinned to the released bezier-path
archive; only an ignored staging copy receives the local extension.

### Pinned toolchain

The development toolchain is deliberately pinned in `scripts/config.sh`:

- Defold 1.13.0 (`f735c12192bf95684e6ae1ae27c400b8170fc6d8`)
- Extender `extender-v2.12.4`
- Xcode, macOS SDK, and iOS SDK 26.2
- Apple Silicon macOS builds via `http://127.0.0.1:9010`

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

Start the foreground watcher from this repository:

```sh
make watch
```

The command starts Extender if needed, performs an initial build, and watches
native `.cpp`, `.h`, and `ext.manifest` files. Saves are debounced. If files
change during a build, one follow-up build is queued. Test or Extender failures
are printed but do not terminate the watcher; fix the source and save again.
Stop watching with Ctrl-C. Extender stays available for later builds and can be
managed explicitly:

```sh
make extender-status
make extender-start
make extender-stop
```

Individual pipeline stages are also available:

```sh
make test       # native regression and pipeline unit tests
make stage-app  # refresh the isolated one-sudoku staging root
make dev-build  # tests, stages, and builds through local Extender
```

`ONE_SUDOKU_ROOT=/absolute/path make dev-build` overrides the default sibling
checkout. Build artifacts and the staged app are stored below `.local-build/`.
Resolved dependency archives from the app are copied into the staging cache
when available. The private dependency URLs use the app's existing
`__GITHUB_TOKEN__` convention. Store the token in the ignored repository-local
`.env` file:

```sh
GITHUB_TOKEN=github_pat_your_token
```

`GITHUB_TOKEN` or `GH_TOKEN` from the process environment takes precedence over
`.env`. The file is parsed as data and is never shell-sourced. During a build,
the token is written only to a mode-0600 temporary Bob settings file, filtered
from Bob output, and deleted afterward; source project files are never modified.

### Hosted production build

Production uses the real `one-sudoku/game.project`, verifies that it is still
pinned to bezier-path 0.2.2, rejects localhost dependencies, and explicitly
uses Defold's hosted build service. It downloads pinned Bob automatically and
does not require Xcode 26.2 or the local Extender:

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
- Temporary SDK package server log: `.local-build/package-server.log`
- Staged app: `.local-build/one-sudoku-stage/`
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
