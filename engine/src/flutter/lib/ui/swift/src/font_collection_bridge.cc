// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/font_collection_bridge.h"
#include "include/swift_bridge_engine_registry.h"

// Flutter engine headers (only in .cc file)
#include "third_party/skia/include/core/SkFontMgr.h"
#include "third_party/skia/include/core/SkStream.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include "txt/asset_font_manager.h"
#include "txt/font_collection.h"
#include "txt/platform.h"
#include "txt/typeface_font_asset_provider.h"

#include <memory>
#include <mutex>
#include <string>

namespace flutter::swift_bridge {

namespace {

/// Global font collection and manager for the Swift bridge.
///
/// DIFFERENCE FROM DART: The Dart version uses UIDartState to access the
/// per-engine FontCollection. The Swift bridge uses a global singleton
/// because there's no Dart VM state to track engine instances.
/// REASON: Simplified architecture for Swift bridge. In a multi-engine
/// scenario, this would need to be extended to track font collections
/// per-engine.
class SwiftFontCollectionManager {
 public:
  static SwiftFontCollectionManager& Instance() {
    static SwiftFontCollectionManager instance;
    return instance;
  }

  /// Loads a font from memory data.
  ///
  /// @param data Pointer to font data
  /// @param length Length of font data in bytes
  /// @param family_name Optional family name override (empty string = use
  /// font's name)
  /// @return True if the font was loaded successfully
  bool LoadFont(const uint8_t* data,
                size_t length,
                const std::string& family_name) {
    if (data == nullptr || length == 0) {
      return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Create a memory stream from the font data (copies the data)
    auto font_stream =
        std::make_unique<SkMemoryStream>(data, length, true /* copyData */);

    // Get the default font manager to create a typeface
    sk_sp<SkFontMgr> font_mgr = txt::GetDefaultFontManager();
    if (!font_mgr) {
      return false;
    }

    // Create a typeface from the font data
    sk_sp<SkTypeface> typeface =
        font_mgr->makeFromStream(std::move(font_stream));
    if (!typeface) {
      return false;
    }

    // Register the typeface with our dynamic font manager
    if (family_name.empty()) {
      dynamic_font_manager_->font_provider().RegisterTypeface(typeface);
    } else {
      dynamic_font_manager_->font_provider().RegisterTypeface(typeface,
                                                              family_name);
    }

    // Clear the cache so the new font is picked up
    font_collection_->ClearFontFamilyCache();

    return true;
  }

  /// Clears the font family cache.
  void ClearCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    font_collection_->ClearFontFamilyCache();
  }

  /// Returns the font collection for use by ParagraphBuilderBridge.
  std::shared_ptr<txt::FontCollection> GetFontCollection() {
    return font_collection_;
  }

 private:
  SwiftFontCollectionManager() {
    // Create the font collection
    font_collection_ = std::make_shared<txt::FontCollection>();

    // Setup the default font manager
    font_collection_->SetupDefaultFontManager(0);

    // Create and set the dynamic font manager using our font provider
    dynamic_font_manager_ =
        sk_make_sp<txt::DynamicFontManager>();
    font_collection_->SetDynamicFontManager(dynamic_font_manager_);
  }

  ~SwiftFontCollectionManager() = default;

  // Non-copyable
  SwiftFontCollectionManager(const SwiftFontCollectionManager&) = delete;
  SwiftFontCollectionManager& operator=(const SwiftFontCollectionManager&) =
      delete;

  std::mutex mutex_;
  std::shared_ptr<txt::FontCollection> font_collection_;
  sk_sp<txt::DynamicFontManager> dynamic_font_manager_;
};

}  // namespace

bool LoadFontFromList(const uint8_t* data,
                      size_t length,
                      const char* family_name) {
  if (data == nullptr || length == 0) return false;
  std::string family_str = family_name ? family_name : "";

  // Also load into the engine's font collection (used by ParagraphBuilder)
  void* engine_fc_ptr = SwiftBridgeEngineRegistry::GetFontCollection();
  if (engine_fc_ptr) {
    auto& engine_fc =
        *static_cast<std::shared_ptr<txt::FontCollection>*>(engine_fc_ptr);
    if (engine_fc) {
      auto stream = std::make_unique<SkMemoryStream>(data, length, true);
      sk_sp<SkFontMgr> mgr = txt::GetDefaultFontManager();
      if (mgr) {
        sk_sp<SkTypeface> face = mgr->makeFromStream(std::move(stream));
        if (face) {
          auto dyn = sk_make_sp<txt::DynamicFontManager>();
          if (family_str.empty())
            dyn->font_provider().RegisterTypeface(face);
          else
            dyn->font_provider().RegisterTypeface(face, family_str);
          engine_fc->SetDynamicFontManager(dyn);
          engine_fc->ClearFontFamilyCache();
        }
      }
    }
  }

  // Also load into the standalone manager (fallback)
  return SwiftFontCollectionManager::Instance().LoadFont(data, length,
                                                         family_str);
}

void ClearFontFamilyCache() {
  SwiftFontCollectionManager::Instance().ClearCache();
}

// Expose the font collection for use by other bridges (like ParagraphBuilder)
std::shared_ptr<txt::FontCollection> GetSwiftBridgeFontCollection() {
  return SwiftFontCollectionManager::Instance().GetFontCollection();
}

}  // namespace flutter::swift_bridge
