// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "export.h"
#include "flutter/lib/ui/swift/src/font_ink_shift.h"
#include "live_objects.h"
#include "third_party/skia/include/core/SkFontMgr.h"
#include "third_party/skia/include/ports/SkFontMgr_empty.h"
#include "third_party/skia/modules/skparagraph/include/FontCollection.h"
#include "third_party/skia/modules/skparagraph/include/TypefaceFontProvider.h"
#include "wrappers.h"

#include <cstdlib>
#include <memory>

using namespace skia::textlayout;
using namespace Skwasm;

SKWASM_EXPORT FlutterFontCollection* fontCollection_create() {
  liveFontCollectionCount++;
  auto collection = sk_make_sp<FontCollection>();
  auto provider = sk_make_sp<TypefaceFontProvider>();
  collection->enableFontFallback();
  collection->setDefaultFontManager(provider, "Roboto");
  return new FlutterFontCollection{
      std::move(collection),
      std::move(provider),
  };
}

SKWASM_EXPORT void fontCollection_dispose(FlutterFontCollection* collection) {
  liveFontCollectionCount--;
  delete collection;
}

static sk_sp<SkFontMgr> default_fontmgr() {
  static sk_sp<SkFontMgr> mgr = SkFontMgr_New_Custom_Empty();
  return mgr;
}

SKWASM_EXPORT SkTypeface* typeface_create(SkData* fontData) {
  liveTypefaceCount++;
  auto typeface = default_fontmgr()->makeFromData(sk_ref_sp<SkData>(fontData));
  return typeface.release();
}

// Face `faceIndex` of a font collection (.ttc/.otc: Noto CJK ships SC, TC,
// JP and KR in one file), the same semantics as the desktop bridge's
// LoadFontFromFileFace: null for an index below 0 or past the last face.
// `typeface_create` is face 0.
SKWASM_EXPORT SkTypeface* typeface_createWithIndex(SkData* fontData,
                                                   int faceIndex) {
  if (faceIndex < 0) {
    return nullptr;
  }
  auto typeface = default_fontmgr()->makeFromData(sk_ref_sp<SkData>(fontData),
                                                  faceIndex);
  if (!typeface) {
    return nullptr;
  }
  liveTypefaceCount++;
  return typeface.release();
}

// Loud probe, as the desktop build's StarlingTextFontFaceIndexSupported: a
// caller that finds this export missing knows the engine is too old.
SKWASM_EXPORT int starling_typeface_faceIndexSupported() {
  return 1;
}

SKWASM_EXPORT void typeface_dispose(SkTypeface* typeface) {
  liveTypefaceCount--;
  typeface->unref();
}

// Calculates the code points that are not covered by the specified typefaces.
// This function mutates the `codePoints` buffer in place and returns the count
// of code points that are not covered by the fonts.
SKWASM_EXPORT int typefaces_filterCoveredCodePoints(SkTypeface** typefaces,
                                                    int typefaceCount,
                                                    SkUnichar* codePoints,
                                                    int codePointCount) {
  std::unique_ptr<SkGlyphID[]> glyphBuffer =
      std::make_unique<SkGlyphID[]>(codePointCount);
  SkGlyphID* glyphPointer = glyphBuffer.get();
  int remainingCodePointCount = codePointCount;
  for (int typefaceIndex = 0; typefaceIndex < typefaceCount; typefaceIndex++) {
    typefaces[typefaceIndex]->unicharsToGlyphs(
        codePoints, remainingCodePointCount, glyphPointer);
    int outputIndex = 0;
    for (int inputIndex = 0; inputIndex < remainingCodePointCount;
         inputIndex++) {
      if (glyphPointer[inputIndex] == 0) {
        if (outputIndex != inputIndex) {
          codePoints[outputIndex] = codePoints[inputIndex];
        }
        outputIndex++;
      }
    }
    if (outputIndex == 0) {
      return 0;
    } else {
      remainingCodePointCount = outputIndex;
    }
  }
  return remainingCodePointCount;
}

SKWASM_EXPORT void fontCollection_registerTypeface(
    FlutterFontCollection* collection,
    SkTypeface* typeface,
    SkString* fontName) {
  if (fontName) {
    SkString alias = *fontName;
    sk_sp<SkTypeface> face = sk_ref_sp<SkTypeface>(typeface);
    // Starling: a family named "Ink+<em> <name>" registers the face drawn
    // <em> of its em lower than its outlines stand, as the native bridge's
    // LoadFontFromFileFace does (font_ink_shift.h).
    if (alias.startsWith("Ink+")) {
      const double shift = std::atof(alias.c_str() + 4);
      if (shift != 0) face = sk_sp<SkTypeface>(new StarlingInkShiftTypeface(face, shift));
    }
    collection->provider->registerTypeface(std::move(face), alias);
  } else {
    collection->provider->registerTypeface(sk_ref_sp<SkTypeface>(typeface));
  }
}

SKWASM_EXPORT void fontCollection_clearCaches(
    FlutterFontCollection* collection) {
  collection->collection->clearCaches();
}
