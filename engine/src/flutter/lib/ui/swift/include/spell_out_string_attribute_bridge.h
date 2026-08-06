// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_SPELL_OUT_STRING_ATTRIBUTE_BRIDGE_H_
#define FLUTTER_SWIFT_SPELL_OUT_STRING_ATTRIBUTE_BRIDGE_H_

#include "swift_bridge_export.h"

#include <swift/bridging>
#include "intrusive_reference_counted.h"

#include <cstdint>

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class SpellOutStringAttributeBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainSpellOutStringAttributeBridge(
    flutter::swift_bridge::SpellOutStringAttributeBridge* p) noexcept;
void ReleaseSpellOutStringAttributeBridge(
    flutter::swift_bridge::SpellOutStringAttributeBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declaration for pimpl
struct SpellOutStringAttributeImpl;

/// C++ bridge wrapping Flutter's SpellOutStringAttribute.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/semantics.dart:1610-1643`
/// **Original:** `class SpellOutStringAttribute extends StringAttribute`
///
/// Stores a spell-out string attribute with start/end range.
/// Maps to NativeStringAttribute::initSpellOutStringAttribute in the engine.
class FLUTTER_SWIFT_BRIDGE_EXPORT
    SWIFT_SHARED_REFERENCE(RetainSpellOutStringAttributeBridge,
                           ReleaseSpellOutStringAttributeBridge)
        SpellOutStringAttributeBridge
    : public IntrusiveReferenceCounted<SpellOutStringAttributeBridge> {
 public:
  /// Creates a SpellOutStringAttributeBridge with the given range.
  ///
  /// Parameters match NativeStringAttribute::initSpellOutStringAttribute
  /// (minus the Dart_Handle which is not needed in Swift).
  SpellOutStringAttributeBridge(int32_t start, int32_t end);

  ~SpellOutStringAttributeBridge();

  /// Returns the start of the text range.
  int32_t GetStart() const;

  /// Returns the end of the text range.
  int32_t GetEnd() const;

 private:
  SpellOutStringAttributeBridge(const SpellOutStringAttributeBridge&) = delete;
  SpellOutStringAttributeBridge& operator=(
      const SpellOutStringAttributeBridge&) = delete;

  // Pimpl - holds the actual Flutter StringAttribute
  SpellOutStringAttributeImpl* impl_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainSpellOutStringAttributeBridge(
    flutter::swift_bridge::SpellOutStringAttributeBridge* p) noexcept {
  p->Retain();
}
inline void ReleaseSpellOutStringAttributeBridge(
    flutter::swift_bridge::SpellOutStringAttributeBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_SPELL_OUT_STRING_ATTRIBUTE_BRIDGE_H_
