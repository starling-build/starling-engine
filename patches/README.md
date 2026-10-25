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
- **`skia-scale-x.patch`** — skparagraph's `TextStyle::setScaleX`: a
  run's font drawn and advanced that much narrower or wider
  (`SkFont::setScaleX` on the shaper's font, which HarfBuzz's positions and
  the drawn glyphs both follow; `equalsByFonts` tells two scales apart, so
  runs and the paragraph cache keep them apart too). Starling: Word for
  Windows' Arabic, set at Arial's widths where the stand-in is Noto Naskh,
  11% wider (Office's Word layout, zh-d97c5474). Reached through txt's
  `scale_x`, the Swift bridge's encoded text style (slot 8, mask bit 20)
  and skwasm's `textStyle_setScaleX`; 1 unless a run asks.
- **`skia-pdf-fake-bold.patch`** — the PDF backend writes a fake-bold run
  (`SkFont::setEmbolden`) as the face's own glyphs, filled and stroked
  (text render mode 2, the stroke Skia's emboldening would add), where the
  emboldened outline is a modified path and made every glyph a Type 3
  drawing. Word for Mac writes its fake bold the same way. Readers group
  Type 3 text badly: PDFKit read Office's bold Chinese headings as lines
  broken where none breaks (zh-b8de3ae2's PMingLiU titles, 286 Type 3
  fonts in one document, 2 now).
