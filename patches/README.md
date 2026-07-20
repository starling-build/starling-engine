# Engine patches

Local patches to the Flutter engine's third-party Skia source. The engine
fetches `engine/src/flutter/third_party/skia` via `gclient sync`, which is
gitignored by the engine repo and overwritten on every sync — so these patches
need to be re-applied after a sync, before rebuilding the engine.

## Workflow

1. After `gclient sync` (or whenever the engine's `third_party/skia` is
   refreshed), run:

   ```
   ./scripts/apply_engine_patches.sh
   ```

   The script is idempotent — patches already applied are skipped.

2. Rebuild the engine:

   ```
   cd engine/src
   PATH="$HOME/dev/depot_tools:$PATH" flutter/bin/et build
   ```

## Patches

- **`skia-stencil-nullcheck.patch`** — defensive null check in
  `GrRenderTarget::numStencilBits` and its caller `GrGLGpu::flushGLState`.
  Prevents a null-deref crash in `libflutter_engine.so` when a `saveLayer` /
  blur op (e.g. `BoxShadow(blurRadius > 0)`, `BackdropFilter`) targets an
  offscreen FBO without a stencil attachment. The original code relies on an
  `SkASSERT` that's compiled out in release builds. Reproduced under the
  `DesktopShellApp` linux_drm shell when Chrome's DMA-BUF surfaces composite
  underneath a shadow / blur.
