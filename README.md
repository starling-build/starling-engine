# starling-engine

Starling's fork of the Flutter engine (plus the flutter monorepo scaffolding the
engine build needs). This repo is the **C++ side** of the Starling desktop: the
DRM/KMS embedder and the Swift bridge. All Swift code — the framework port, the
shell, the apps — lives in the sibling repo **starling-desktop**, which consumes
this repo's build artifacts.

## History: a real fork of flutter/flutter

This repo is a **GitHub fork of `flutter/flutter`**, so it carries upstream's
full history, tags and branches. Starling's work lives on the **`starling`**
branch (the default), as ordinary commits on top of the release it is based on:

```
starling            ← Starling's commits
  ⋮
3.38.6 (8b87286)    ← the upstream release we are based on
  ⋮                   ...all of upstream's history below
```

So the Starling delta is `git diff 3.38.6..starling` — 184 files: the
`linux_drm` embedder, the Swift bridge, build wiring and port tooling. **Keep
it small:** prefer adding files under the Starling-owned directories to editing
upstream ones. New files cannot conflict on a rebase; edits to upstream files
are the only thing that can.

### Rebasing onto a newer Flutter

Ordinary git, because the ancestry is real:

```bash
git fetch upstream --tags                      # upstream = flutter/flutter
git rebase --onto 3.41.0 3.38.6 starling       # replays only our commits
gclient sync                                   # DEPS moved
cd engine/src && flutter/tools/gn --runtime-mode=debug --no-lto --no-backtrace --no-rbe
ninja -C out/host_debug libflutter_engine.so libflutter_linux_drm.so
```

Rebase onto **release tags, not `main`** — DEPS, the prebuilt Dart SDK and the
bridge headers that starling-desktop compiles against all have to stay
coherent. Expect a full rebuild after a rebase (~4400 steps per config), and
re-run `gclient sync` first because DEPS will have moved.

GitHub Actions is disabled on this fork: upstream's workflows are Google CI
that cannot run here.

### Before the fork

The repo previously held a two-commit snapshot (a squashed import of 3.38.6
plus one delta commit) with no upstream ancestry, which made rebasing
impossible. That lineage is preserved at `starling-build/starling-engine-snapshot`.
The snapshot turned out to be byte-identical to upstream 3.38.6 apart from the
omitted `.github/workflows` and three Windows files whose CRLF the import had
flattened to LF; rebasing onto the real tag restored all of them.

## What Starling added / owns

| path | what |
|---|---|
| `engine/src/flutter/shell/platform/linux_drm/` | DRM/KMS embedder: modeset, GBM/EGL, libinput, libseat, multi-view (`fl_drm_view` C API) |
| `engine/src/flutter/lib/ui/swift/` | libswift_bridge — the engine ↔ Swift framework boundary (on Linux it is merged into `libflutter_engine.so`) |

Everything else is upstream Flutter, kept structurally intact so `gclient`/GN
still work.

## Build

Hydrate DEPS once (needs depot_tools on PATH). Copy
`engine/scripts/standard.gclient` to `.gclient` with the URL pointed at this
repo, then:

```bash
gclient sync
```

Generate the build files once — `out/` is untracked, so a fresh clone has no
`args.gn`:

```bash
cd engine/src
flutter/tools/gn --runtime-mode=debug   --no-lto --no-backtrace --no-rbe
flutter/tools/gn --runtime-mode=release --no-lto --no-backtrace --no-rbe
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
