// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_SEMANTICS_FLAGS_BRIDGE_H_
#define FLUTTER_SWIFT_SEMANTICS_FLAGS_BRIDGE_H_

#include <swift/bridging>
#include "intrusive_reference_counted.h"

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class SemanticsFlagsBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainSemanticsFlagsBridge(
    flutter::swift_bridge::SemanticsFlagsBridge* p) noexcept;
void ReleaseSemanticsFlagsBridge(
    flutter::swift_bridge::SemanticsFlagsBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declaration for pimpl
struct SemanticsFlagsImpl;

/// C++ bridge wrapping Flutter's SemanticsFlags.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/semantics.dart:1159-1550`
/// **Original:** `class SemanticsFlags extends NativeFieldWrapperClass1`
///
/// Stores the semantics flag values and provides access to them.
/// The 7 tristate/checked fields are stored as int (enum raw values),
/// and the 16 boolean fields are stored as bool.
class __attribute__((visibility("default")))
    SWIFT_SHARED_REFERENCE(RetainSemanticsFlagsBridge,
                           ReleaseSemanticsFlagsBridge)
        SemanticsFlagsBridge
    : public IntrusiveReferenceCounted<SemanticsFlagsBridge> {
 public:
  /// Creates a SemanticsFlagsBridge with the given flag values.
  ///
  /// Parameters match NativeSemanticsFlags::initSemanticsFlags
  /// (minus the Dart_Handle which is not needed in Swift).
  ///
  /// The first 7 int parameters are enum raw values:
  ///   isChecked: SemanticsCheckState (0=none, 1=true, 2=false, 3=mixed)
  ///   isSelected..isFocused: SemanticsTristate (0=none, 1=true, 2=false)
  SemanticsFlagsBridge(int isChecked,
                       int isSelected,
                       int isEnabled,
                       int isToggled,
                       int isExpanded,
                       int isRequired,
                       int isFocused,
                       bool isButton,
                       bool isTextField,
                       bool isInMutuallyExclusiveGroup,
                       bool isHeader,
                       bool isObscured,
                       bool scopesRoute,
                       bool namesRoute,
                       bool isHidden,
                       bool isImage,
                       bool isLiveRegion,
                       bool hasImplicitScrolling,
                       bool isMultiline,
                       bool isReadOnly,
                       bool isLink,
                       bool isSlider,
                       bool isKeyboardKey);

  ~SemanticsFlagsBridge();

  // Tristate/checked getters (return raw int enum values)
  int GetIsChecked() const;
  int GetIsSelected() const;
  int GetIsEnabled() const;
  int GetIsToggled() const;
  int GetIsExpanded() const;
  int GetIsRequired() const;
  int GetIsFocused() const;

  // Boolean flag getters
  bool GetIsButton() const;
  bool GetIsTextField() const;
  bool GetIsInMutuallyExclusiveGroup() const;
  bool GetIsHeader() const;
  bool GetIsObscured() const;
  bool GetScopesRoute() const;
  bool GetNamesRoute() const;
  bool GetIsHidden() const;
  bool GetIsImage() const;
  bool GetIsLiveRegion() const;
  bool GetHasImplicitScrolling() const;
  bool GetIsMultiline() const;
  bool GetIsReadOnly() const;
  bool GetIsLink() const;
  bool GetIsSlider() const;
  bool GetIsKeyboardKey() const;

 private:
  SemanticsFlagsBridge(const SemanticsFlagsBridge&) = delete;
  SemanticsFlagsBridge& operator=(const SemanticsFlagsBridge&) = delete;

  // Pimpl - holds the actual Flutter SemanticsFlags struct
  SemanticsFlagsImpl* impl_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainSemanticsFlagsBridge(
    flutter::swift_bridge::SemanticsFlagsBridge* p) noexcept {
  p->Retain();
}
inline void ReleaseSemanticsFlagsBridge(
    flutter::swift_bridge::SemanticsFlagsBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_SEMANTICS_FLAGS_BRIDGE_H_
