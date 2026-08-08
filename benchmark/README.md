# Native rendering benchmark

Run the engine-independent raster benchmark with:

```sh
make -C benchmark run
```

Run the stable-handle, cache-invalidation, cropped-render, edge-case, and
scratch-memory regression tests with:

```sh
make -C benchmark test
```

The scoped-path and cropped-texture Lua helpers have a standalone LuaJIT test:

```sh
make -C benchmark lua-test LUA=/path/to/luajit
```

The executable is written to `/private/tmp`. Timings include raster masks,
temporary allocations, compositing, and blur, but exclude Lua, Defold buffer
copies, and GPU texture uploads.

When the pre-optimization ARM64 library is available, reproduce the recorded
baseline with `make -C benchmark legacy LEGACY_LIBRARY=/path/to/library.a`.

On the same ARM64 machine used for the original baseline, the acceptance gates
are:

- Classic workload: no more than 31 ms (baseline 156 ms).
- Every 512-pixel circle operation: at least 5x faster than its baseline.
- Shared raster scratch at 512x512: no more than 4 MiB.
- 64-pixel controls: no regression greater than 10% from the recorded
  pre-change release build.

The renderer uses one shared 16-bit sample mask and two reusable float buffers,
so the worst-case 512x512 shadow scratch is approximately 2.5 MiB.
