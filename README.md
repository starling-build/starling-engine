# starling-engine

Starling's fork of the Flutter engine (plus the flutter monorepo scaffolding the
engine build needs). This repo is the **C++ side** of the Starling desktop: the
DRM/KMS embedder and the Swift bridge. All Swift code — the framework port, the
shell, the apps — lives in the sibling repo **starling-desktop**, which consumes
this repo's build artifacts.

## History

Two commits, deliberately:

1. **Import upstream Flutter 3.38.6** — a pristine snapshot of
   `flutter/flutter` @ `8b87286`, the commit Starling forked from.
2. **Starling engine changes** — everything Starling adds, as one reviewable
   diff (203 files: the `linux_drm` embedder, the Swift bridge, build wiring,
   port tooling).

So `git diff HEAD~1 HEAD` is exactly the Starling delta, and rebasing onto a
newer upstream has a defined base to compare against.

(`.github/workflows` is omitted from both: Google CI that cannot run here, and
pushing workflow files needs a token scope this repo's automation lacks.)

## What Starling added / owns

| path | what |
|---|---|
| `engine/src/flutter/shell/platform/linux_drm/` | DRM/KMS embedder: modeset, GBM/EGL, libinput, libseat, multi-view (`fl_drm_view` C API) |
| `engine/src/flutter/lib/ui/swift/` | libswift_bridge — the engine ↔ Swift framework boundary (on Linux it is merged into `libflutter_engine.so`) |

Everything else is upstream Flutter, kept structurally intact so `gclient`/GN
still work.

## Build

Hydrate DEPS once (needs depot_tools on PATH):

```bash
# .gclient solution pointing at this repo, then:
gclient sync
```

Incremental build (seconds):

```bash
ninja -C engine/src/out/host_debug   libflutter_linux_drm.so libflutter_engine.so
ninja -C engine/src/out/host_release libflutter_linux_drm.so libflutter_engine.so
```

Rebuild **both** host_debug and host_release when changing the engine —
dev runs use host_debug (host_release when present), packaging uses
host_release.

## What starling-desktop consumes

- `engine/src/out/<config>/libflutter_engine.so`, `libflutter_linux_drm.so`, `icudtl.dat`
- headers: `engine/src/flutter/lib/ui/swift/include/*.h` (via modulemap),
  the embedder API, `fl_drm_view.h`
- consumed via a symlink at the desktop repo root: `engine -> ../starling-engine/engine`

The Swift shell needs **no relink** after an engine rebuild — it binds only the
stable C API.

## Invariants shared with starling-desktop

- `EvdevToHID` (`fl_drm_input.cc`) and `WaylandIntegration.hidToEvdev` (desktop
  repo) are exact inverses. **Change one, change the other**, or letters break
  for Wayland clients.

## Known quirks

- The macOS-only GN target `build_swift_demo` (`shell/common/BUILD.gn`) expects
  a `flutter_swift/` directory two levels above the GN root — that directory now
  lives in starling-desktop (`sdk/`). Linux builds never evaluate it.
- Dart framework / tool portions of the monorepo (`packages/`, `bin/`, `dev/`)
  are retained because the engine build and asset tooling still reference parts
  of them; slimming is future work.
