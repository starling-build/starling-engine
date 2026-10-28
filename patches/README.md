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
- **`skia-long-word-next-line.patch`** — skparagraph's TextWrapper sends a
  word longer than a line to the next line first, unless it starts its line,
  and breaks it there where it fills — as Word and browsers do. Upstream broke
  it in place as soon as two of its clusters stood on the current line, so a
  long unbreakable token (a mangled symbol, a hash, a joined Thai or Han
  stretch) began at the end of a line and was chopped at the edge. URLs and
  paths are not affected: ICU breaks them after "/" and "-". Approved by the
  user (2026-10-26, via macos).
- **`skia-no-line-spacing-shift.patch`** — skparagraph's `TextLine` no
  longer shifts each line right by half its first cluster's letter spacing
  (upstream's "TODO: This is the fix for flutter. Must be removed...").
  `Run::addSpacesEvenly` already puts the spacing after each glyph, as Word
  does, so the shift was the only difference: -0.3pt on condensed body
  lines, +1.9 on a line opening with an autospaced character (windows,
  chasing zh-5be7e156; the user's approval via macos, 2026-10-25). Apply
  after `skia-justify-ideographic-space.patch`, which edits the same file
  (the script's sorted order does).
- **`skia-shaper-scale-x.patch`** — SkShaper's HarfBuzz sub-font gets the
  font at scale 1: its advance callbacks read `getWidths`, which already
  carry the scale, and `shapeRun` multiplies HarfBuzz's positions by
  `getScaleX()` again, so without it a run at 0.5 advanced a quarter as wide
  (lenovo-3's probe on Linux, 91.18 -> 22.80). A patch of its own because
  `apply_engine_patches.sh` skips a patch it finds applied: a hunk added to
  `skia-scale-x.patch` never reached a checkout that had the first version.
  A fix to an existing patch goes in a new file the same way.
- **`skia-pdf-fake-bold.patch`** — the PDF backend writes a fake-bold run
  (`SkFont::setEmbolden`) as the face's own glyphs, filled and stroked
  (text render mode 2, the stroke Skia's emboldening would add), where the
  emboldened outline is a modified path and made every glyph a Type 3
  drawing. Word for Mac writes its fake bold the same way. Readers group
  Type 3 text badly: PDFKit read Office's bold Chinese headings as lines
  broken where none breaks (zh-b8de3ae2's PMingLiU titles, 286 Type 3
  fonts in one document, 2 now).

- **`skia-freetype-linear-size.patch`** — preserve the requested floating
  point font size in FreeType's linear glyph advances. The 26.6 size passed
  to `FT_Set_Char_Size` truncates 14⅔px to 14.65625px; eight Carlito “M”s
  then advance 100.24646px instead of CoreText's 100.31775px. Restore the
  ratio from the actual FreeType size to the requested size for linear
  advances only, leaving hinted outlines and bitmap strikes alone. The
  existing subpixel-size matrix correction still handles sizes below 1px.
  This removes the Mac native/skwasm word-boringcrypto wrap differences at
  screen scale, while its 220px PDF font already uses an exact 26.6 size.
