// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/semantics_flags_bridge.h"

// Note: We cannot include flutter/lib/ui/semantics/semantics_flags.h because
// it pulls in Dart VM dependencies (dart_wrapper.h -> tonic).
// Instead, we define the storage struct locally. The enum values and struct
// layout match flutter::SemanticsFlags exactly.
// See: flutter/lib/ui/semantics/semantics_flags.h:13-49

namespace flutter::swift_bridge {

// Pimpl implementation - stores the flag values directly.
// Mirrors flutter::SemanticsFlags struct layout from semantics_flags.h:25-49
// without depending on the Dart VM header chain.
struct SemanticsFlagsImpl {
  // Tristate/checked values stored as int (matching enum raw values)
  int isChecked;     // SemanticsCheckState: 0=none, 1=true, 2=false, 3=mixed
  int isSelected;    // SemanticsTristate: 0=none, 1=true, 2=false
  int isEnabled;     // SemanticsTristate
  int isToggled;     // SemanticsTristate
  int isExpanded;    // SemanticsTristate
  int isRequired;    // SemanticsTristate
  int isFocused;     // SemanticsTristate

  // Boolean flags
  bool isButton;
  bool isTextField;
  bool isInMutuallyExclusiveGroup;
  bool isHeader;
  bool isObscured;
  bool scopesRoute;
  bool namesRoute;
  bool isHidden;
  bool isImage;
  bool isLiveRegion;
  bool hasImplicitScrolling;
  bool isMultiline;
  bool isReadOnly;
  bool isLink;
  bool isSlider;
  bool isKeyboardKey;
};

SemanticsFlagsBridge::SemanticsFlagsBridge(int isChecked,
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
                                           bool isKeyboardKey) {
  // Same field assignment as NativeSemanticsFlags::initSemanticsFlags
  // (semantics_flags.cc:48-72)
  impl_ = new SemanticsFlagsImpl{
      isChecked,
      isSelected,
      isEnabled,
      isToggled,
      isExpanded,
      isRequired,
      isFocused,
      isButton,
      isTextField,
      isInMutuallyExclusiveGroup,
      isHeader,
      isObscured,
      scopesRoute,
      namesRoute,
      isHidden,
      isImage,
      isLiveRegion,
      hasImplicitScrolling,
      isMultiline,
      isReadOnly,
      isLink,
      isSlider,
      isKeyboardKey,
  };
}

SemanticsFlagsBridge::~SemanticsFlagsBridge() {
  delete impl_;
}

// Tristate/checked getters
int SemanticsFlagsBridge::GetIsChecked() const {
  return impl_->isChecked;
}

int SemanticsFlagsBridge::GetIsSelected() const {
  return impl_->isSelected;
}

int SemanticsFlagsBridge::GetIsEnabled() const {
  return impl_->isEnabled;
}

int SemanticsFlagsBridge::GetIsToggled() const {
  return impl_->isToggled;
}

int SemanticsFlagsBridge::GetIsExpanded() const {
  return impl_->isExpanded;
}

int SemanticsFlagsBridge::GetIsRequired() const {
  return impl_->isRequired;
}

int SemanticsFlagsBridge::GetIsFocused() const {
  return impl_->isFocused;
}

// Boolean flag getters
bool SemanticsFlagsBridge::GetIsButton() const {
  return impl_->isButton;
}

bool SemanticsFlagsBridge::GetIsTextField() const {
  return impl_->isTextField;
}

bool SemanticsFlagsBridge::GetIsInMutuallyExclusiveGroup() const {
  return impl_->isInMutuallyExclusiveGroup;
}

bool SemanticsFlagsBridge::GetIsHeader() const {
  return impl_->isHeader;
}

bool SemanticsFlagsBridge::GetIsObscured() const {
  return impl_->isObscured;
}

bool SemanticsFlagsBridge::GetScopesRoute() const {
  return impl_->scopesRoute;
}

bool SemanticsFlagsBridge::GetNamesRoute() const {
  return impl_->namesRoute;
}

bool SemanticsFlagsBridge::GetIsHidden() const {
  return impl_->isHidden;
}

bool SemanticsFlagsBridge::GetIsImage() const {
  return impl_->isImage;
}

bool SemanticsFlagsBridge::GetIsLiveRegion() const {
  return impl_->isLiveRegion;
}

bool SemanticsFlagsBridge::GetHasImplicitScrolling() const {
  return impl_->hasImplicitScrolling;
}

bool SemanticsFlagsBridge::GetIsMultiline() const {
  return impl_->isMultiline;
}

bool SemanticsFlagsBridge::GetIsReadOnly() const {
  return impl_->isReadOnly;
}

bool SemanticsFlagsBridge::GetIsLink() const {
  return impl_->isLink;
}

bool SemanticsFlagsBridge::GetIsSlider() const {
  return impl_->isSlider;
}

bool SemanticsFlagsBridge::GetIsKeyboardKey() const {
  return impl_->isKeyboardKey;
}

}  // namespace flutter::swift_bridge
