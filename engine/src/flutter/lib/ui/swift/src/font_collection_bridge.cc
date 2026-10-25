// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/core/SkTypefaceCache.h"
#include "include/font_collection_bridge.h"
#include "include/swift_bridge_engine_registry.h"

// Flutter engine headers (only in .cc file)
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkFontMgr.h"
#include "third_party/skia/include/core/SkStream.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include "txt/asset_font_manager.h"
#include "txt/font_collection.h"
#include "txt/platform.h"
#include "txt/typeface_font_asset_provider.h"
#include "flutter/fml/icu_util.h"

#include <cstdio>

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
  /// Never destroyed. Its collection's destructor purges SkShaper's
  /// HarfBuzz face cache, a function-local static made at the first shaping
  /// — after this one, so torn down before it at exit. Destroying the
  /// manager then reads the freed cache: on Windows, flutter_engine.dll's
  /// detach died with 0xC0000005 after a 19-page export had been written.
  static SwiftFontCollectionManager& Instance() {
    static SwiftFontCollectionManager* instance =
        new SwiftFontCollectionManager();
    return *instance;
  }

  /// Registers an already-created typeface. The caller makes ONE typeface
  /// (over one SkData) and hands the same sk_sp here and to the engine's
  /// collection — a typeface carries its bytes with it, so sharing the face
  /// is what keeps a font's data in memory once rather than per-collection.
  bool RegisterFace(const sk_sp<SkTypeface>& typeface,
                    const std::string& family_name) {
    if (!typeface) {
      return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

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

/// This library carries its own copy of Skia, and so its own
/// `SkTypefaceCache::NewTypefaceID()` counter, starting at 1 — as does the
/// engine's copy. A typeface this library creates (every font an app loads
/// through LoadFontFromFile) therefore gets the same uniqueID as some
/// typeface the engine created (a system font the chrome draws with). The
/// engine's rasterizer keys its glyph strikes by that ID plus the size, so
/// the moment both typefaces are drawn at one device size, the second one
/// is painted from the first one's strike: glyphs of the wrong face, or
/// the right glyphs at the wrong advances. Seen as "Liberation Sans at
/// exactly 14pt draws stretched" — 14 being the chrome's body size — and
/// as a 10.5pt document paragraph rendering as accented capitals. Moving
/// this copy's counter far past anything the engine will ever allocate
/// keeps the two ID spaces apart. Runs once, at load, before any typeface
/// exists.
static const bool g_typeface_ids_offset = [] {
  for (int i = 0; i < (1 << 22); ++i) {
    SkTypefaceCache::NewTypefaceID();
  }
  return true;
}();

/// The dynamic font manager installed in the *engine's* font collection —
/// the one ParagraphBuilder resolves families through, and therefore the one
/// that decides what every widget is measured and painted with.
///
/// It has to outlive a single LoadFontFromList call. `SetDynamicFontManager`
/// is one slot, not a list, so minting a fresh manager per font and setting
/// it dropped every font registered before this one: an app loading a regular
/// and a bold face kept only the bold, and one loading four faces across two
/// families kept only the last. The family the app actually asked for then
/// resolved to nothing.
///
/// That failure is near-silent. A style carrying a `fontFamilyFallback` that
/// happens to name the surviving family still paints — in the wrong face, at
/// the wrong weight — while a style *without* a fallback (a TextPainter used
/// to measure a monospace cell, say) falls through to the platform's default
/// proportional font and comes back a wildly different width. Rendering looks
/// plausible and the metrics do not agree with it.
std::mutex g_engine_font_mutex;
sk_sp<txt::DynamicFontManager> g_engine_dynamic_fonts;
const txt::FontCollection* g_engine_fonts_owner = nullptr;

/// One typeface into every collection that resolves families: the engine's
/// (ParagraphBuilder) and the standalone manager (measurement and tests that
/// run before an engine exists). The same sk_sp goes to both — this used to
/// build a separate SkMemoryStream(copy=true) per collection, so every
/// font's bytes sat in anonymous RSS once per registration; the terminal's
/// CJK fallback alone was 2x19.5 MB of the app's footprint.
bool RegisterFaceEverywhere(const sk_sp<SkTypeface>& face,
                            const std::string& family_str) {
  if (!face) return false;

  void* engine_fc_ptr = SwiftBridgeEngineRegistry::GetFontCollection();
  if (engine_fc_ptr) {
    auto& engine_fc =
        *static_cast<std::shared_ptr<txt::FontCollection>*>(engine_fc_ptr);
    if (engine_fc) {
      std::lock_guard<std::mutex> lock(g_engine_font_mutex);
      // Install one manager per collection and keep registering into it.
      // The provider accumulates (family -> style set), so successive
      // calls add faces instead of replacing them, and weights within a
      // family stay distinguishable.
      if (!g_engine_dynamic_fonts || g_engine_fonts_owner != engine_fc.get()) {
        g_engine_dynamic_fonts = sk_make_sp<txt::DynamicFontManager>();
        g_engine_fonts_owner = engine_fc.get();
        engine_fc->SetDynamicFontManager(g_engine_dynamic_fonts);
      }
      if (family_str.empty())
        g_engine_dynamic_fonts->font_provider().RegisterTypeface(face);
      else
        g_engine_dynamic_fonts->font_provider().RegisterTypeface(face,
                                                                 family_str);
      // Rebuilds the resolved-family cache so the face just added is
      // visible to the next layout.
      engine_fc->ClearFontFamilyCache();
    }
  }

  return SwiftFontCollectionManager::Instance().RegisterFace(face, family_str);
}

}  // namespace

bool LoadFontFromList(const uint8_t* data,
                      size_t length,
                      const char* family_name) {
  if (data == nullptr || length == 0) return false;
  sk_sp<SkFontMgr> mgr = txt::GetDefaultFontManager();
  if (!mgr) return false;
  // ONE copy of the bytes, shared by the typeface in every collection. The
  // caller's buffer does not outlive this call, so a copy there must be —
  // callers that have a PATH should use LoadFontFromFile, which has none.
  sk_sp<SkData> bytes = SkData::MakeWithCopy(data, length);
  sk_sp<SkTypeface> face = mgr->makeFromData(std::move(bytes));
  return RegisterFaceEverywhere(face, family_name ? family_name : "");
}

bool LoadFontFromFile(const char* path, const char* family_name) {
  if (path == nullptr || *path == '\0') return false;
  // mmap, not read: the font stays file-backed — shared across every process
  // that loads it and evictable under pressure, so it costs page cache, not
  // anonymous RSS. FreeType reads through the mapping on demand.
  sk_sp<SkData> bytes = SkData::MakeFromFileName(path);
  if (!bytes) return false;
  sk_sp<SkFontMgr> mgr = txt::GetDefaultFontManager();
  if (!mgr) return false;
  sk_sp<SkTypeface> face = mgr->makeFromData(std::move(bytes));
  return RegisterFaceEverywhere(face, family_name ? family_name : "");
}

void ClearFontFamilyCache() {
  SwiftFontCollectionManager::Instance().ClearCache();
}

bool InitializeICU(const char* icu_data_path) {
  if (icu_data_path == nullptr || icu_data_path[0] == '\0') {
    return false;
  }
  // fml::icu::InitializeICU FML_CHECKs (aborts) on an unreadable path; a
  // host that guessed a path wrong deserves `false`, not a dead process.
  FILE* probe = std::fopen(icu_data_path, "rb");
  if (probe == nullptr) {
    return false;
  }
  std::fclose(probe);
  fml::icu::InitializeICU(std::string(icu_data_path));
  return true;
}

// Expose the font collection for use by other bridges (like ParagraphBuilder)
std::shared_ptr<txt::FontCollection> GetSwiftBridgeFontCollection() {
  return SwiftFontCollectionManager::Instance().GetFontCollection();
}

}  // namespace flutter::swift_bridge
