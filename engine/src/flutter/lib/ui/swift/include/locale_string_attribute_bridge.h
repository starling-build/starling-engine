// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_LOCALE_STRING_ATTRIBUTE_BRIDGE_H_
#define FLUTTER_SWIFT_LOCALE_STRING_ATTRIBUTE_BRIDGE_H_

#include "swift_bridge_export.h"

#include <swift/bridging>
#include "intrusive_reference_counted.h"

#include <cstdint>

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class LocaleStringAttributeBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainLocaleStringAttributeBridge(
    flutter::swift_bridge::LocaleStringAttributeBridge* p) noexcept;
void ReleaseLocaleStringAttributeBridge(
    flutter::swift_bridge::LocaleStringAttributeBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declaration for pimpl
struct LocaleStringAttributeImpl;

/// C++ bridge wrapping Flutter's LocaleStringAttribute.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/semantics.dart:1645-1683`
/// **Original:** `class LocaleStringAttribute extends StringAttribute`
///
/// Stores a locale string attribute with start/end range and locale tag.
/// Maps to NativeStringAttribute::initLocaleStringAttribute in the engine.
class FLUTTER_SWIFT_BRIDGE_EXPORT
    SWIFT_SHARED_REFERENCE(RetainLocaleStringAttributeBridge,
                           ReleaseLocaleStringAttributeBridge)
        LocaleStringAttributeBridge
    : public IntrusiveReferenceCounted<LocaleStringAttributeBridge> {
 public:
  /// Creates a LocaleStringAttributeBridge with the given range and locale.
  ///
  /// Parameters match NativeStringAttribute::initLocaleStringAttribute
  /// (minus the Dart_Handle which is not needed in Swift).
  /// Uses const char* instead of std::string to avoid ABI mismatch between
  /// Swift (std::__1::) and Flutter engine (std::_fl::).
  LocaleStringAttributeBridge(int32_t start, int32_t end,
                              const char* locale);

  ~LocaleStringAttributeBridge();

  /// Returns the start of the text range.
  int32_t GetStart() const;

  /// Returns the end of the text range.
  int32_t GetEnd() const;

  /// Returns the locale language tag string.
  /// Returns a pointer to internal storage - valid for the lifetime of this object.
  const char* GetLocale() const;

 private:
  LocaleStringAttributeBridge(const LocaleStringAttributeBridge&) = delete;
  LocaleStringAttributeBridge& operator=(
      const LocaleStringAttributeBridge&) = delete;

  // Pimpl - holds the actual Flutter StringAttribute
  LocaleStringAttributeImpl* impl_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainLocaleStringAttributeBridge(
    flutter::swift_bridge::LocaleStringAttributeBridge* p) noexcept {
  p->Retain();
}
inline void ReleaseLocaleStringAttributeBridge(
    flutter::swift_bridge::LocaleStringAttributeBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_LOCALE_STRING_ATTRIBUTE_BRIDGE_H_
