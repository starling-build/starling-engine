# starling-engine — project guide

**This repo is the C++ half of one product.** It is Starling's fork of the
Flutter engine: the DRM/KMS embedder and the engine↔Swift bridge. Everything
Swift — the framework port, the desktop shell, the apps, the packaging —
lives in the sibling repo **starling-desktop**, which consumes this repo's
build artifacts.

| | path | what lives there |
|---|---|---|
| **starling-engine** (this repo) | `~/dev/starling-build/starling-engine` | the Flutter engine fork: `linux_drm` embedder, `lib/ui/swift` bridge, GN/ninja build |
| **starling-desktop** | `~/dev/starling-build/starling-desktop` | `sdk/` (Flutter→Swift framework port), `shell/`, `apps/`, `build/` (staging + .deb) |

If a task is about windows, dock, compositor, theming, spaces, apps, input, or
anything the user sees — **the code is in starling-desktop, not here.** Read its
`CLAUDE.md` too. A feature usually touches both repos and both get committed.

The older `starling-os` repo still holds the Bazel-built immutable Linux distro
(kernel, sysroot, disk image) and its QEMU boot gates. The desktop's own dev
loop no longer depends on it.

## What Starling owns here

```
engine/src/flutter/shell/platform/linux_drm/   the DRM/KMS embedder (22 files)
    fl_drm_view.{cc,h}        multi-view, the public fl_drm_view C API
    fl_drm_display / _swap_chain / _egl / _gbm    modeset, page flip, buffers
    fl_drm_input.cc           libinput + xkbcommon  ← see EvdevToHID trap below
    fl_drm_seat.{cc,h}        libseat (seatd/logind) — unprivileged device access
    fl_drm_cursor / _task_runner / _engine
engine/src/flutter/lib/ui/swift/               libswift_bridge (69 files)
    include/*_bridge.h        the headers starling-desktop's sdk/ compiles against
```

Everything else in the tree is upstream Flutter, kept structurally intact so
`gclient`/GN keep working and upstream rebases stay possible.

## History: upstream base + one delta commit

```
Starling engine changes         ← everything we add, ONE reviewable diff
Import upstream Flutter 3.38.6  ← pristine flutter/flutter @ 8b87286
```

`git diff HEAD~1 HEAD` is exactly the Starling delta (203 files). **Keep it that
way:** when you must edit an upstream file, do it deliberately and minimally —
every unnecessary change to upstream code is permanent noise in that diff and
extra conflict surface on the next rebase. Prefer adding files under the two
Starling-owned directories over modifying upstream ones.

`.github/workflows` is absent by design: Google CI that cannot run here, and
pushing workflow files needs a token scope this repo's automation lacks. Don't
re-add it — the push will be rejected.

## Build

Dependencies come from `gclient` (`.gclient` at the repo root is local, not
tracked). On a fresh clone, with `~/dev/depot_tools` on PATH:

```bash
gclient sync          # hydrates DEPS: clang toolchain, prebuilt Dart SDK, third_party
```

Then generate and build. `out/` is untracked, so **a fresh clone has no
`args.gn`** — `flutter/tools/gn` is what writes it (and then runs `gn gen`; the
`gn` binary itself lives at `engine/src/flutter/third_party/gn/gn`, **not** in
`buildtools/`, which only has clang):

```bash
cd engine/src
flutter/tools/gn --runtime-mode=debug   --no-lto --no-backtrace --no-rbe
flutter/tools/gn --runtime-mode=release --no-lto --no-backtrace --no-rbe
ninja -C out/host_debug   libflutter_linux_drm.so libflutter_engine.so
ninja -C out/host_release libflutter_linux_drm.so libflutter_engine.so
```

Once `args.gn` exists, regenerating is just `flutter/third_party/gn/gn gen
out/<config>`. Incremental rebuilds are seconds; a full config is ~4400 steps
(~8 min on 12 cores). **Rebuild BOTH configs**: the dev runner prefers
`host_release` when it exists and packaging requires it, so a debug-only
rebuild silently tests stale code.

Building both repos on a machine with nothing installed —  apt packages,
depot_tools, toolchains, timings, Ubuntu 26.04 workarounds — is written up in
starling-desktop's `docs/BUILDING.md`.

## What starling-desktop consumes

- `engine/src/out/<config>/libflutter_engine.so`, `libflutter_linux_drm.so`, `icudtl.dat`
- headers: `lib/ui/swift/include/*.h` (via modulemap), the embedder API, `fl_drm_view.h`

It reaches all of it through a symlink at its own repo root —
`engine -> ../starling-engine/engine`, created by its `./bootstrap.sh`. After
building here, nothing else is needed there: the Swift shell binds only the
stable C API, so **an engine rebuild needs no shell relink**.

## Traps that have cost real time

- **`EvdevToHID` (`fl_drm_input.cc`) and `WaylandIntegration.hidToEvdev`
  (starling-desktop's shell) are exact inverses. Change one, change the other**,
  or letters break for Wayland clients. This is the one invariant that spans
  both repos.
- **Moving this checkout costs a full rebuild.** ninja keys its cache on the
  absolute paths in compile command lines, and `out/*/args.gn` carries an
  absolute `default_git_folder`. Relocating means rewriting args.gn, re-running
  `gn gen`, ~4300 steps per config, and relinking every Swift binary in
  starling-desktop (its rpath is absolute). Pick the final path before building.
- Env knobs are read with a bare `getenv()`, so an **empty string is not the
  same as unset** — `FLUTTER_DRM_CONNECTOR=""` makes the connector filter
  reject every output (`[DRM] No connected connector found` on a working
  display). Callers must forward optional vars only when non-empty.
- The macOS-only GN target `build_swift_demo` (`shell/common/BUILD.gn`) expects
  a `flutter_swift/` directory two levels above the GN root; that code now lives
  in starling-desktop as `sdk/`. Linux builds never evaluate it.

## Standing directions

- **Never post code or file contents to external paste services.**
- Dart framework/tool portions of the monorepo (`packages/`, `bin/`, `dev/`) are
  retained because the engine build and asset tooling still reference parts of
  them. Slimming them is future work — don't delete opportunistically.
