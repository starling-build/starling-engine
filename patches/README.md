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
- **`skia-justify-ideographic-space.patch`** — skparagraph's
  `TextLine::justify` counts an ideographic space (U+3000) as an ideograph
  rather than a stretchable space. Justification hands every space's width
  to the stretch and gives the spaces a line opens with none at all, so a
  Chinese line's "　" shrank to the step and a justified paragraph opening
  with "　　" began its first line two ems into the margin (Office's Word
  layout: zh-1d3d66d3 and three sibling reports, 12pt left of Word's print).
  Word keeps U+3000 an em, spaced like the ideographs beside it.
- **`skia-fractional-line-heights.patch`** — skparagraph's
  `ParagraphStyle::setFractionalLineHeights`: a paragraph that asks keeps
  each line's height with its fraction, where skparagraph otherwise rounds
  every line to a whole layout unit (`InternalLineMetrics::height`). Word
  does not round: a 12pt PMingLiU line is 1332/1024 em, 15.61pt, and a
  page of them reaches its foot where Word's does only with the fraction
  (Office's Word layout: zh-de3410bd's page 1 ends two lines early in
  Word's print, as it does here only so). Off by default; the Swift bridge
  sets it from bit 13 of the encoded paragraph style, and Office asks for
  it in a Word document's paragraphs only.
