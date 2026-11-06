// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef FLUTTER_LIB_UI_SWIFT_SRC_FONT_INK_SHIFT_H_
#define FLUTTER_LIB_UI_SWIFT_SRC_FONT_INK_SHIFT_H_

#include <memory>
#include <optional>

#include "third_party/skia/include/core/SkFontMetrics.h"
#include "third_party/skia/include/core/SkFontTypes.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include "third_party/skia/src/core/SkAdvancedTypefaceMetrics.h"
#include "third_party/skia/src/core/SkFontDescriptor.h"
#include "third_party/skia/src/core/SkGlyph.h"
#include "third_party/skia/src/core/SkScalerContext.h"

// Global scope on purpose: Starling's Skia patch (skia-ink-shift-friend) makes
// these two names friends of SkTypeface and SkScalerContext.

// ---------------------------------------------------------------------------
// A stand-in face whose ink is drawn lower (or higher) than its outlines'
// own place, by a fraction of the em, with advances, metrics, tables and
// the glyph-to-character map those of the face it wraps. Starling: Office
// draws PMingLiU, which Word's print sets about 0.07 em lower than Noto
// Serif CJK TC's outlines on the same baseline, with the stand-in under
// the family name "Ink+0.07 <name>" (see LoadFontFromFileFace). Layout and
// the PDF's text layer (origins, widths, ToUnicode) are untouched; only the
// glyph shapes move — in the PDF the Type3 glyph procedures, since the
// outlines are not embeddable as TrueType.
// ---------------------------------------------------------------------------
class StarlingInkShiftScalerContext : public SkScalerContext {
 public:
  StarlingInkShiftScalerContext(SkTypeface& face,
                        sk_sp<SkTypeface> proxy,
                        double shift_em,
                        const SkScalerContextEffects& effects,
                        const SkDescriptor* desc)
      : SkScalerContext(face, effects, desc),
        proxy_(proxy->createScalerContext(SkScalerContextEffects(), desc)),
        shift_em_(shift_em) {}

 protected:
  GlyphMetrics generateMetrics(const SkGlyph& orig, SkArenaAlloc* alloc) override {
    SkGlyph glyph = proxy_->internalMakeGlyph(orig.getPackedID(), SkMask::kA8_Format, alloc);
    GlyphMetrics mx(SkMask::kA8_Format);
    mx.advance = glyph.advanceVector();
    mx.extraBits = glyph.extraBits();
    proxy_->getPath(glyph, alloc);
    if (!glyph.path()) {
      mx.neverRequestPath = true;
      mx.bounds = SkRect::MakeEmpty();
      return mx;
    }
    mx.generatedPath = GeneratedPath{glyph.path()->makeOffset(0, Dy()), true};
    mx.computeFromPath = true;
    return mx;
  }

  void generateImage(const SkGlyph& glyph, void* image) override {
    this->generateImageFromPath(glyph, image);
  }

  std::optional<GeneratedPath> generatePath(const SkGlyph& glyph) override {
    std::optional<GeneratedPath> p = proxy_->generatePath(glyph);
    if (!p) return std::nullopt;
    return GeneratedPath{p->path.makeOffset(0, Dy()), true};
  }

  sk_sp<SkDrawable> generateDrawable(const SkGlyph&) override { return nullptr; }

  void generateFontMetrics(SkFontMetrics* metrics) override {
    proxy_->getFontMetrics(metrics);
  }

 private:
  SkScalar Dy() const {
    return static_cast<SkScalar>(shift_em_ * fRec.fTextSize * fRec.fPost2x2[1][1]);
  }
  std::unique_ptr<SkScalerContext> proxy_;
  double shift_em_;
};

class StarlingInkShiftTypeface : public SkTypeface {
 public:
  StarlingInkShiftTypeface(sk_sp<SkTypeface> proxy, double shift_em)
      : SkTypeface(proxy->fontStyle(), proxy->isFixedPitch()),
        proxy_(std::move(proxy)),
        shift_em_(shift_em) {}

 protected:
  std::unique_ptr<SkScalerContext> onCreateScalerContext(
      const SkScalerContextEffects& effects,
      const SkDescriptor* desc) const override {
    return std::make_unique<StarlingInkShiftScalerContext>(
        *const_cast<StarlingInkShiftTypeface*>(this), proxy_, shift_em_, effects, desc);
  }
  void onFilterRec(SkScalerContextRec* rec) const override {
    proxy_->filterRec(rec);
    // Glyphs come from the shifted path, whatever the mask format asked.
    rec->setHinting(SkFontHinting::kNone);
  }
  void getGlyphToUnicodeMap(SkSpan<SkUnichar> d) const override {
    proxy_->getGlyphToUnicodeMap(d);
  }
  std::unique_ptr<SkAdvancedTypefaceMetrics> onGetAdvancedMetrics() const override {
    auto m = proxy_->getAdvancedMetrics();
    // The PDF must draw these outlines (shifted) as Type3 glyphs, never embed
    // the unshifted font file.
    if (m) m->fFlags |= SkAdvancedTypefaceMetrics::kNotEmbeddable_FontFlag;
    return m;
  }
  std::unique_ptr<SkStreamAsset> onOpenStream(int* ttcIndex) const override {
    return proxy_->openStream(ttcIndex);
  }
  sk_sp<SkTypeface> onMakeClone(const SkFontArguments& args) const override {
    sk_sp<SkTypeface> p = proxy_->makeClone(args);
    return p ? sk_sp<SkTypeface>(new StarlingInkShiftTypeface(p, shift_em_)) : nullptr;
  }
  void onGetFontDescriptor(SkFontDescriptor* d, bool* local) const override {
    proxy_->getFontDescriptor(d, local);
  }
  void onCharsToGlyphs(SkSpan<const SkUnichar> u, SkSpan<SkGlyphID> g) const override {
    proxy_->unicharsToGlyphs(u, g);
  }
  int onCountGlyphs() const override { return proxy_->countGlyphs(); }
  int onGetUPEM() const override { return proxy_->getUnitsPerEm(); }
  void onGetFamilyName(SkString* n) const override { proxy_->getFamilyName(n); }
  bool onGetPostScriptName(SkString* n) const override {
    return proxy_->getPostScriptName(n);
  }
  SkTypeface::LocalizedStrings* onCreateFamilyNameIterator() const override {
    return proxy_->createFamilyNameIterator();
  }
  void getPostScriptGlyphNames(SkString* n) const override {
    proxy_->getPostScriptGlyphNames(n);
  }
  bool onGlyphMaskNeedsCurrentColor() const override {
    return proxy_->glyphMaskNeedsCurrentColor();
  }
  int onGetVariationDesignPosition(
      SkSpan<SkFontArguments::VariationPosition::Coordinate> c) const override {
    return proxy_->onGetVariationDesignPosition(c);
  }
  int onGetVariationDesignParameters(
      SkSpan<SkFontParameters::Variation::Axis> a) const override {
    return proxy_->onGetVariationDesignParameters(a);
  }
  int onGetTableTags(SkSpan<SkFontTableTag> t) const override {
    return proxy_->readTableTags(t);
  }
  size_t onGetTableData(SkFontTableTag tag, size_t off, size_t len, void* data) const override {
    return proxy_->getTableData(tag, off, len, data);
  }

 private:
  sk_sp<SkTypeface> proxy_;
  double shift_em_;
};


#endif  // FLUTTER_LIB_UI_SWIFT_SRC_FONT_INK_SHIFT_H_
