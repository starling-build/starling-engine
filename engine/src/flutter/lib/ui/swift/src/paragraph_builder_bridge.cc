// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/paragraph_builder_bridge.h"
#include "include/paragraph_bridge.h"
#include "include/swift_bridge_engine_registry.h"

// Flutter engine headers (only in .cc file)
#include "flutter/fml/logging.h"
#include "flutter/txt/src/txt/font_collection.h"
#include "flutter/txt/src/txt/font_style.h"
#include "flutter/txt/src/txt/font_weight.h"
#include "flutter/txt/src/txt/paragraph.h"
#include "flutter/txt/src/txt/paragraph_builder.h"
#include "flutter/txt/src/txt/paragraph_style.h"
#include "flutter/txt/src/txt/placeholder_run.h"
#include "flutter/txt/src/txt/text_baseline.h"
#include "flutter/txt/src/txt/text_decoration.h"
#include "flutter/txt/src/txt/text_style.h"
#include "third_party/icu/source/common/unicode/ustring.h"
#include "third_party/skia/include/core/SkColor.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace flutter::swift_bridge {

namespace {

const double kTextHeightNone = 0.0;

// TextStyle encoding constants (from paragraph_builder.cc)
const int kTSLeadingDistributionIndex = 0;
const int kTSColorIndex = 1;
const int kTSTextDecorationIndex = 2;
const int kTSTextDecorationColorIndex = 3;
const int kTSTextDecorationStyleIndex = 4;
const int kTSFontWeightIndex = 5;
const int kTSFontStyleIndex = 6;
// Note: kTSTextBaselineIndex (7) is defined in the original paragraph_builder.cc
// but is not actually used (TODO in paragraph_builder.cc line 418-420).
const int kTSTextDecorationThicknessIndex = 8;
const int kTSFontFamilyIndex = 9;
const int kTSFontSizeIndex = 10;
const int kTSLetterSpacingIndex = 11;
const int kTSWordSpacingIndex = 12;
const int kTSHeightIndex = 13;
const int kTSLocaleIndex = 14;
const int kTSBackgroundIndex = 15;
const int kTSForegroundIndex = 16;
const int kTSTextShadowsIndex = 17;
const int kTSFontFeaturesIndex = 18;
const int kTSFontVariationsIndex = 19;

const int kTSLeadingDistributionMask = 1 << kTSLeadingDistributionIndex;
const int kTSColorMask = 1 << kTSColorIndex;
const int kTSTextDecorationMask = 1 << kTSTextDecorationIndex;
const int kTSTextDecorationColorMask = 1 << kTSTextDecorationColorIndex;
const int kTSTextDecorationStyleMask = 1 << kTSTextDecorationStyleIndex;
const int kTSTextDecorationThicknessMask = 1 << kTSTextDecorationThicknessIndex;
const int kTSFontWeightMask = 1 << kTSFontWeightIndex;
const int kTSFontStyleMask = 1 << kTSFontStyleIndex;
// Note: kTSTextBaselineMask is defined in the original paragraph_builder.cc
// but is not actually used (TODO in paragraph_builder.cc line 418-420).
// We keep the index for documentation but don't use the mask.
const int kTSFontFamilyMask = 1 << kTSFontFamilyIndex;
const int kTSFontSizeMask = 1 << kTSFontSizeIndex;
const int kTSLetterSpacingMask = 1 << kTSLetterSpacingIndex;
const int kTSWordSpacingMask = 1 << kTSWordSpacingIndex;
const int kTSHeightMask = 1 << kTSHeightIndex;
const int kTSLocaleMask = 1 << kTSLocaleIndex;
const int kTSBackgroundMask = 1 << kTSBackgroundIndex;
const int kTSForegroundMask = 1 << kTSForegroundIndex;
const int kTSTextShadowsMask = 1 << kTSTextShadowsIndex;
const int kTSFontFeaturesMask = 1 << kTSFontFeaturesIndex;
const int kTSFontVariationsMask = 1 << kTSFontVariationsIndex;

// ParagraphStyle encoding constants (from paragraph_builder.cc)
const int kPSTextAlignIndex = 1;
const int kPSTextDirectionIndex = 2;
const int kPSFontWeightIndex = 3;
const int kPSFontStyleIndex = 4;
const int kPSMaxLinesIndex = 5;
const int kPSTextHeightBehaviorIndex = 6;
const int kPSFontFamilyIndex = 7;
const int kPSFontSizeIndex = 8;
const int kPSHeightIndex = 9;
const int kPSStrutStyleIndex = 10;
const int kPSEllipsisIndex = 11;
const int kPSLocaleIndex = 12;

const int kPSTextAlignMask = 1 << kPSTextAlignIndex;
const int kPSTextDirectionMask = 1 << kPSTextDirectionIndex;
const int kPSFontWeightMask = 1 << kPSFontWeightIndex;
const int kPSFontStyleMask = 1 << kPSFontStyleIndex;
const int kPSMaxLinesMask = 1 << kPSMaxLinesIndex;
const int kPSFontFamilyMask = 1 << kPSFontFamilyIndex;
const int kPSFontSizeMask = 1 << kPSFontSizeIndex;
const int kPSHeightMask = 1 << kPSHeightIndex;
const int kPSTextHeightBehaviorMask = 1 << kPSTextHeightBehaviorIndex;
const int kPSStrutStyleMask = 1 << kPSStrutStyleIndex;
const int kPSEllipsisMask = 1 << kPSEllipsisIndex;
const int kPSLocaleMask = 1 << kPSLocaleIndex;

// TextShadows decoding constants
constexpr uint32_t kColorDefault = 0xFF000000;
constexpr uint32_t kBytesPerShadow = 16;
constexpr uint32_t kShadowPropertiesCount = 4;
constexpr uint32_t kColorOffset = 0;
constexpr uint32_t kXOffset = 1;
constexpr uint32_t kYOffset = 2;
constexpr uint32_t kBlurOffset = 3;

// FontFeature decoding constants
constexpr uint32_t kBytesPerFontFeature = 8;
constexpr uint32_t kFontFeatureTagLength = 4;

// FontVariation decoding constants
constexpr uint32_t kBytesPerFontVariation = 8;
constexpr uint32_t kFontVariationTagLength = 4;

// Strut decoding constants
const int kSFontWeightIndex = 0;
const int kSFontStyleIndex = 1;
const int kSFontFamilyIndex = 2;
const int kSLeadingDistributionIndex = 3;
const int kSFontSizeIndex = 4;
const int kSHeightIndex = 5;
const int kSLeadingIndex = 6;
const int kSForceStrutHeightIndex = 7;

const int kSFontWeightMask = 1 << kSFontWeightIndex;
const int kSFontStyleMask = 1 << kSFontStyleIndex;
const int kSFontFamilyMask = 1 << kSFontFamilyIndex;
const int kSLeadingDistributionMask = 1 << kSLeadingDistributionIndex;
const int kSFontSizeMask = 1 << kSFontSizeIndex;
const int kSHeightMask = 1 << kSHeightIndex;
const int kSLeadingMask = 1 << kSLeadingIndex;
const int kSForceStrutHeightMask = 1 << kSForceStrutHeightIndex;

// Decode strut style from byte data.
// Matching decodeStrut() in paragraph_builder.cc
void DecodeStrut(const uint8_t* strut_data,
                 size_t strut_data_length,
                 const std::vector<std::string>& strut_font_families,
                 txt::ParagraphStyle& paragraph_style) {
  if (strut_data == nullptr || strut_data_length == 0) {
    return;
  }

  paragraph_style.strut_enabled = true;

  uint8_t mask = strut_data[0];

  size_t byte_count = 1;
  if (mask & kSFontWeightMask) {
    paragraph_style.strut_font_weight =
        static_cast<txt::FontWeight>(strut_data[byte_count++]);
  }
  if (mask & kSFontStyleMask) {
    paragraph_style.strut_font_style =
        static_cast<txt::FontStyle>(strut_data[byte_count++]);
  }

  paragraph_style.strut_half_leading = mask & kSLeadingDistributionMask;

  // Parse float data starting after the int8 values
  std::vector<float> float_data;
  size_t float_bytes = strut_data_length - byte_count;
  float_data.resize(float_bytes / 4);
  if (float_data.size() > 0) {
    memcpy(float_data.data(), strut_data + byte_count, float_bytes);
  }

  size_t float_count = 0;
  if (mask & kSFontSizeMask) {
    paragraph_style.strut_font_size = float_data[float_count++];
  }
  if (mask & kSHeightMask) {
    paragraph_style.strut_height = float_data[float_count++];
    paragraph_style.strut_has_height_override = true;
  }
  if (mask & kSLeadingMask) {
    paragraph_style.strut_leading = float_data[float_count++];
  }

  paragraph_style.force_strut_height = mask & kSForceStrutHeightMask;

  if (mask & kSFontFamilyMask) {
    paragraph_style.strut_font_families = strut_font_families;
  } else {
    paragraph_style.strut_font_families.push_back("");
  }
}

// Decode text shadows from byte data.
// Matching decodeTextShadows() in paragraph_builder.cc
void DecodeTextShadows(const uint8_t* shadows_data,
                       size_t shadows_data_length,
                       std::vector<txt::TextShadow>& decoded_shadows) {
  decoded_shadows.clear();

  if (shadows_data == nullptr || shadows_data_length == 0) {
    return;
  }

  if (shadows_data_length % kBytesPerShadow != 0) {
    return;
  }

  const uint32_t* uint_data = reinterpret_cast<const uint32_t*>(shadows_data);
  const float* float_data = reinterpret_cast<const float*>(shadows_data);

  size_t shadow_count = shadows_data_length / kBytesPerShadow;
  for (size_t shadow_index = 0; shadow_index < shadow_count; ++shadow_index) {
    size_t shadow_count_offset = shadow_index * kShadowPropertiesCount;
    SkColor color =
        uint_data[shadow_count_offset + kColorOffset] ^ kColorDefault;
    decoded_shadows.emplace_back(
        color,
        SkPoint::Make(float_data[shadow_count_offset + kXOffset],
                      float_data[shadow_count_offset + kYOffset]),
        float_data[shadow_count_offset + kBlurOffset]);
  }
}

// Decode font features from byte data.
// Matching decodeFontFeatures() in paragraph_builder.cc
void DecodeFontFeatures(const uint8_t* font_features_data,
                        size_t font_features_data_length,
                        txt::FontFeatures& font_features) {
  if (font_features_data == nullptr || font_features_data_length == 0) {
    return;
  }

  if (font_features_data_length % kBytesPerFontFeature != 0) {
    return;
  }

  size_t feature_count = font_features_data_length / kBytesPerFontFeature;
  for (size_t feature_index = 0; feature_index < feature_count;
       ++feature_index) {
    size_t feature_offset = feature_index * kBytesPerFontFeature;
    const char* feature_bytes =
        reinterpret_cast<const char*>(font_features_data) + feature_offset;
    std::string tag(feature_bytes, kFontFeatureTagLength);
    int32_t value = *reinterpret_cast<const int32_t*>(feature_bytes +
                                                      kFontFeatureTagLength);
    font_features.SetFeature(tag, value);
  }
}

// Decode font variations from byte data.
// Matching decodeFontVariations() in paragraph_builder.cc
void DecodeFontVariations(const uint8_t* font_variations_data,
                          size_t font_variations_data_length,
                          txt::FontVariations& font_variations) {
  if (font_variations_data == nullptr || font_variations_data_length == 0) {
    return;
  }

  if (font_variations_data_length % kBytesPerFontVariation != 0) {
    return;
  }

  size_t variation_count = font_variations_data_length / kBytesPerFontVariation;
  for (size_t variation_index = 0; variation_index < variation_count;
       ++variation_index) {
    size_t variation_offset = variation_index * kBytesPerFontVariation;
    const char* variation_bytes =
        reinterpret_cast<const char*>(font_variations_data) + variation_offset;
    std::string tag(variation_bytes, kFontVariationTagLength);
    float value = *reinterpret_cast<const float*>(variation_bytes +
                                                  kFontVariationTagLength);
    font_variations.SetAxisValue(tag, value);
  }
}

// Helper to get font collection from the engine registry.
std::shared_ptr<txt::FontCollection> GetFontCollection() {
  void* ptr = SwiftBridgeEngineRegistry::GetFontCollection();
  if (ptr) {
    return *static_cast<std::shared_ptr<txt::FontCollection>*>(ptr);
  }
  return nullptr;
}

}  // namespace

// Pimpl implementation holding the actual Flutter types.
struct ParagraphBuilderImpl {
  std::unique_ptr<txt::ParagraphBuilder> builder;

  explicit ParagraphBuilderImpl(std::unique_ptr<txt::ParagraphBuilder> b)
      : builder(std::move(b)) {}
};

ParagraphBuilderBridge::ParagraphBuilderBridge(
    const int32_t* encoded_style,
    size_t encoded_style_length,
    const char* font_family,
    double font_size,
    double height,
    const char* ellipsis,
    const char* locale,
    const uint8_t* strut_data,
    size_t strut_data_length,
    const char* const* strut_font_families,
    size_t strut_font_families_count) {
  // Decode ParagraphStyle from encoded data
  txt::ParagraphStyle style;

  if (encoded_style != nullptr && encoded_style_length >= 7) {
    int32_t mask = encoded_style[0];

    if (mask & kPSTextAlignMask) {
      style.text_align =
          static_cast<txt::TextAlign>(encoded_style[kPSTextAlignIndex]);
    }

    if (mask & kPSTextDirectionMask) {
      style.text_direction =
          static_cast<txt::TextDirection>(encoded_style[kPSTextDirectionIndex]);
    }

    if (mask & kPSFontWeightMask) {
      style.font_weight =
          static_cast<txt::FontWeight>(encoded_style[kPSFontWeightIndex]);
    }

    if (mask & kPSFontStyleMask) {
      style.font_style =
          static_cast<txt::FontStyle>(encoded_style[kPSFontStyleIndex]);
    }

    if (mask & kPSFontFamilyMask) {
      style.font_family = font_family ? font_family : "";
    }

    if (mask & kPSFontSizeMask) {
      style.font_size = font_size;
    }

    if (mask & kPSHeightMask) {
      style.height = height;
      style.has_height_override = true;
    }

    if (mask & kPSTextHeightBehaviorMask) {
      style.text_height_behavior = encoded_style[kPSTextHeightBehaviorIndex];
    }

    if (mask & kPSMaxLinesMask) {
      style.max_lines = encoded_style[kPSMaxLinesIndex];
    }

    // Decode strut style
    if (mask & kPSStrutStyleMask) {
      std::vector<std::string> strut_families;
      for (size_t i = 0; i < strut_font_families_count; ++i) {
        if (strut_font_families[i]) {
          strut_families.push_back(strut_font_families[i]);
        }
      }
      DecodeStrut(strut_data, strut_data_length, strut_families, style);
    }

    if (mask & kPSEllipsisMask) {
      // Convert UTF-8 ellipsis to UTF-16
      if (ellipsis) {
        std::string utf8_ellipsis(ellipsis);
        // Simple ASCII conversion - proper UTF-8 to UTF-16 would need ICU
        style.ellipsis = std::u16string(utf8_ellipsis.begin(),
                                        utf8_ellipsis.end());
      }
    }

    if (mask & kPSLocaleMask) {
      style.locale = locale ? locale : "";
    }
  }

  // Create the paragraph builder using the font collection from the engine
  // registry. The font collection is set by Shell::CreateShellOnPlatformThreadSwift
  // during engine initialization.
  auto font_collection = GetFontCollection();
  if (font_collection) {
    // Not a constant: it decides whether each laid-out blob is also converted
    // to an Impeller TextFrame, and the rasteriser that receives the display
    // list requires exactly one of the two. It was `false` while every Swift
    // host rendered through Skia; iOS has no Skia backend to fall back to, so
    // the shell now reports what it actually built.
    auto builder = txt::ParagraphBuilder::CreateSkiaBuilder(
        style, font_collection,
        /*impeller_enabled=*/SwiftBridgeEngineRegistry::GetImpellerEnabled());
    if (!builder) {
      FML_LOG(ERROR) << "ParagraphBuilderBridge: CreateSkiaBuilder returned "
                        "null; text will not lay out.";
    }
    impl_ = new ParagraphBuilderImpl(std::move(builder));
  } else {
    // Worth saying out loud: without it the only symptom is AddTextSafe
    // returning false, which the Swift side reports as "Invalid text provided
    // to addText" — pointing at the text rather than at engine startup, which
    // is where the fault actually is.
    FML_LOG(ERROR) << "ParagraphBuilderBridge: no font collection registered; "
                      "shell startup did not reach "
                      "SwiftBridgeEngineRegistry::SetFontCollection.";
    impl_ = new ParagraphBuilderImpl(nullptr);
  }
}

ParagraphBuilderBridge::~ParagraphBuilderBridge() {
  delete impl_;
}

void ParagraphBuilderBridge::PushStyle(
    const int32_t* encoded_style,
    size_t encoded_style_length,
    const char* const* font_families,
    size_t font_families_count,
    double font_size,
    double letter_spacing,
    double word_spacing,
    double height,
    double decoration_thickness,
    const char* locale,
    const uint8_t* shadows_data,
    size_t shadows_data_length,
    const uint8_t* font_features_data,
    size_t font_features_data_length,
    const uint8_t* font_variations_data,
    size_t font_variations_data_length,
    bool has_background,
    bool has_foreground,
    uint32_t background_color,
    uint32_t foreground_color) {
  if (!impl_ || !impl_->builder) {
    return;
  }

  if (encoded_style == nullptr || encoded_style_length < 9) {
    return;
  }

  int32_t mask = encoded_style[0];

  // Start with the current style from the stack
  txt::TextStyle style = impl_->builder->PeekStyle();

  style.half_leading = mask & kTSLeadingDistributionMask;

  if (mask & kTSColorMask) {
    style.color = encoded_style[kTSColorIndex];
  }

  if (mask & kTSTextDecorationMask) {
    style.decoration =
        static_cast<txt::TextDecoration>(encoded_style[kTSTextDecorationIndex]);
  }

  if (mask & kTSTextDecorationColorMask) {
    style.decoration_color = encoded_style[kTSTextDecorationColorIndex];
  }

  if (mask & kTSTextDecorationStyleMask) {
    style.decoration_style = static_cast<txt::TextDecorationStyle>(
        encoded_style[kTSTextDecorationStyleIndex]);
  }

  if (mask & kTSTextDecorationThicknessMask) {
    style.decoration_thickness_multiplier = decoration_thickness;
  }

  if (mask & kTSFontWeightMask) {
    style.font_weight =
        static_cast<txt::FontWeight>(encoded_style[kTSFontWeightIndex]);
  }

  if (mask & kTSFontStyleMask) {
    style.font_style =
        static_cast<txt::FontStyle>(encoded_style[kTSFontStyleIndex]);
  }

  if (mask & kTSFontSizeMask) {
    style.font_size = font_size;
  }

  if (mask & kTSLetterSpacingMask) {
    style.letter_spacing = letter_spacing;
  }

  if (mask & kTSWordSpacingMask) {
    style.word_spacing = word_spacing;
  }

  if (mask & kTSHeightMask) {
    style.height = height;
    style.has_height_override = style.height != kTextHeightNone;
  }

  if (mask & kTSLocaleMask) {
    style.locale = locale ? locale : "";
  }

  if (mask & kTSBackgroundMask) {
    if (has_background) {
      // Create a simple background paint with the given color
      DlPaint dl_paint;
      dl_paint.setColor(DlColor(background_color));
      style.background = dl_paint;
    }
  }

  if (mask & kTSForegroundMask) {
    if (has_foreground) {
      // Create a simple foreground paint with the given color
      DlPaint dl_paint;
      dl_paint.setColor(DlColor(foreground_color));
      style.foreground = dl_paint;
    }
  }

  if (mask & kTSTextShadowsMask) {
    DecodeTextShadows(shadows_data, shadows_data_length, style.text_shadows);
  }

  if (mask & kTSFontFamilyMask) {
    std::vector<std::string> families;
    for (size_t i = 0; i < font_families_count; ++i) {
      if (font_families[i]) {
        families.push_back(font_families[i]);
      }
    }
    style.font_families = families;
  }

  if (mask & kTSFontFeaturesMask) {
    DecodeFontFeatures(font_features_data, font_features_data_length,
                       style.font_features);
  }

  if (mask & kTSFontVariationsMask) {
    DecodeFontVariations(font_variations_data, font_variations_data_length,
                         style.font_variations);
  }

  impl_->builder->PushStyle(style);
}

void ParagraphBuilderBridge::Pop() {
  if (impl_ && impl_->builder) {
    impl_->builder->Pop();
  }
}

const char* ParagraphBuilderBridge::AddText(const char* text) {
  if (!impl_ || !impl_->builder) {
    return "ParagraphBuilder is not initialized";
  }

  if (text == nullptr || *text == '\0') {
    return nullptr;
  }

  // Add text directly using the UTF-8 API
  impl_->builder->AddText(reinterpret_cast<const uint8_t*>(text), strlen(text));

  return nullptr;
}

bool ParagraphBuilderBridge::AddTextSafe(const char* text) {
  if (!impl_ || !impl_->builder) {
    return false;
  }

  if (text == nullptr || *text == '\0') {
    return true;  // Empty text is valid
  }

  // Add text directly using the UTF-8 API
  impl_->builder->AddText(reinterpret_cast<const uint8_t*>(text), strlen(text));

  return true;
}

void ParagraphBuilderBridge::AddPlaceholder(double width,
                                            double height,
                                            int alignment,
                                            double baseline_offset,
                                            int baseline) {
  if (!impl_ || !impl_->builder) {
    return;
  }

  txt::PlaceholderRun placeholder_run(
      width, height, static_cast<txt::PlaceholderAlignment>(alignment),
      static_cast<txt::TextBaseline>(baseline), baseline_offset);

  impl_->builder->AddPlaceholder(placeholder_run);
}

ParagraphBridge* ParagraphBuilderBridge::Build() {
  if (!impl_ || !impl_->builder) {
    return nullptr;
  }

  std::unique_ptr<txt::Paragraph> paragraph = impl_->builder->Build();
  impl_->builder.reset();

  // Create ParagraphBridge from the built paragraph
  // We need to pass ownership via a void pointer
  return new ParagraphBridge(static_cast<void*>(&paragraph));
}

}  // namespace flutter::swift_bridge
