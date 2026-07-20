// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/semantics_update_bridge.h"

// Note: We cannot include flutter/lib/ui/semantics/semantics_update.h because
// it pulls in Dart VM dependencies (dart_wrapper.h -> tonic).
// Instead, we define a local storage struct that holds the same data types.
// See: flutter/lib/ui/semantics/semantics_node.h for SemanticsNodeUpdates
// See: flutter/lib/ui/semantics/custom_accessibility_action.h for
//   CustomAccessibilityActionUpdates

// We CAN include these engine headers since they don't depend on Dart VM
#include "third_party/skia/include/core/SkM44.h"
#include "third_party/skia/include/core/SkRect.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace flutter::swift_bridge {

// Local definitions mirroring flutter::StringAttribute hierarchy
// from flutter/lib/ui/semantics/string_attribute.h
// Cannot include original because it depends on dart_wrapper.h
enum class StringAttributeType : int32_t {
  kSpellOut,
  kLocale,
};

struct StringAttribute {
  virtual ~StringAttribute() = default;
  int32_t start = -1;
  int32_t end = -1;
  StringAttributeType type;
};

struct SpellOutStringAttribute : StringAttribute {};

struct LocaleStringAttribute : StringAttribute {
  std::string locale;
};

using StringAttributePtr = std::shared_ptr<StringAttribute>;
using StringAttributes = std::vector<StringAttributePtr>;

// Local definition mirroring flutter::SemanticsFlags
// from flutter/lib/ui/semantics/semantics_flags.h:25-49
enum class SemanticsCheckState : int32_t {
  kNone = 0,
  kTrue = 1,
  kFalse = 2,
  kMixed = 3,
};

enum class SemanticsTristate : int32_t {
  kNone = 0,
  kTrue = 1,
  kFalse = 2,
};

struct SemanticsFlags {
  SemanticsCheckState isChecked = SemanticsCheckState::kNone;
  SemanticsTristate isSelected = SemanticsTristate::kNone;
  SemanticsTristate isEnabled = SemanticsTristate::kNone;
  SemanticsTristate isToggled = SemanticsTristate::kNone;
  SemanticsTristate isExpanded = SemanticsTristate::kNone;
  SemanticsTristate isRequired = SemanticsTristate::kNone;
  SemanticsTristate isFocused = SemanticsTristate::kNone;
  bool isButton = false;
  bool isTextField = false;
  bool isInMutuallyExclusiveGroup = false;
  bool isHeader = false;
  bool isObscured = false;
  bool scopesRoute = false;
  bool namesRoute = false;
  bool isHidden = false;
  bool isImage = false;
  bool isLiveRegion = false;
  bool hasImplicitScrolling = false;
  bool isMultiline = false;
  bool isReadOnly = false;
  bool isLink = false;
  bool isSlider = false;
  bool isKeyboardKey = false;
};

// Local definition mirroring flutter::SemanticsRole
// from flutter/lib/ui/semantics/semantics_node.h:74-108
enum class SemanticsRole : int32_t {
  kNone = 0,
  kTab = 1,
  kTabBar = 2,
  kTabPanel = 3,
  kDialog = 4,
  kAlertDialog = 5,
  kTable = 6,
  kCell = 7,
  kRow = 8,
  kColumnHeader = 9,
  kDragHandle = 10,
  kSpinButton = 11,
  kComboBox = 12,
  kMenuBar = 13,
  kMenu = 14,
  kMenuItem = 15,
  kMenuItemCheckbox = 16,
  kMenuItemRadio = 17,
  kList = 18,
  kListItem = 19,
  kForm = 20,
  kTooltip = 21,
  kLoadingSpinner = 22,
  kProgressBar = 23,
  kHotKey = 24,
  kRadioGroup = 25,
  kStatus = 26,
  kAlert = 27,
  kComplementary = 28,
  kContentInfo = 29,
  kMain = 30,
  kNavigation = 31,
  kRegion = 32,
};

// Local definition mirroring flutter::SemanticsValidationResult
// from flutter/lib/ui/semantics/semantics_node.h:116-120
enum class SemanticsValidationResult : int32_t {
  kNone = 0,
  kValid = 1,
  kInvalid = 2,
};

// Local definition mirroring flutter::SemanticsNode
// from flutter/lib/ui/semantics/semantics_node.h:122-173
struct SemanticsNode {
  int32_t id = 0;
  SemanticsFlags flags;
  int32_t actions = 0;
  int32_t maxValueLength = -1;
  int32_t currentValueLength = -1;
  int32_t textSelectionBase = -1;
  int32_t textSelectionExtent = -1;
  int32_t platformViewId = -1;
  int32_t scrollChildren = 0;
  int32_t scrollIndex = 0;
  double scrollPosition = 0.0;
  double scrollExtentMax = 0.0;
  double scrollExtentMin = 0.0;
  std::string identifier;
  std::string label;
  StringAttributes labelAttributes;
  std::string hint;
  StringAttributes hintAttributes;
  std::string value;
  StringAttributes valueAttributes;
  std::string increasedValue;
  StringAttributes increasedValueAttributes;
  std::string decreasedValue;
  StringAttributes decreasedValueAttributes;
  std::string tooltip;
  int32_t textDirection = 0;  // 0=unknown, 1=rtl, 2=ltr

  SkRect rect = SkRect::MakeEmpty();
  SkM44 transform = SkM44{};
  std::vector<int32_t> childrenInTraversalOrder;
  std::vector<int32_t> childrenInHitTestOrder;
  std::vector<int32_t> customAccessibilityActions;
  int32_t headingLevel = 0;

  std::string linkUrl;
  SemanticsRole role = SemanticsRole::kNone;
  SemanticsValidationResult validationResult = SemanticsValidationResult::kNone;
  std::string locale;
};

using SemanticsNodeUpdates = std::unordered_map<int32_t, SemanticsNode>;

// Local definition mirroring flutter::CustomAccessibilityAction
// from flutter/lib/ui/semantics/custom_accessibility_action.h:17-25
struct CustomAccessibilityAction {
  int32_t id = 0;
  int32_t overrideId = -1;
  std::string label;
  std::string hint;
};

using CustomAccessibilityActionUpdates =
    std::unordered_map<int32_t, CustomAccessibilityAction>;

// Pimpl implementation holding the accumulated updates.
struct SemanticsUpdateImpl {
  SemanticsNodeUpdates nodes;
  CustomAccessibilityActionUpdates actions;

  SemanticsUpdateImpl(SemanticsNodeUpdates n,
                      CustomAccessibilityActionUpdates a)
      : nodes(std::move(n)), actions(std::move(a)) {}
};

SemanticsUpdateBridge::SemanticsUpdateBridge(void* opaque_nodes,
                                             void* opaque_actions) {
  // Move the data from the builder into our impl
  auto* nodes = static_cast<SemanticsNodeUpdates*>(opaque_nodes);
  auto* actions = static_cast<CustomAccessibilityActionUpdates*>(opaque_actions);
  impl_ = new SemanticsUpdateImpl(std::move(*nodes), std::move(*actions));
}

SemanticsUpdateBridge::~SemanticsUpdateBridge() {
  delete impl_;
}

void SemanticsUpdateBridge::Dispose() {
  // Clear the data, matching SemanticsUpdate::dispose() which calls
  // ClearDartWrapper(). In Swift, we just clear the internal data.
  if (impl_) {
    impl_->nodes.clear();
    impl_->actions.clear();
  }
}

const void* SemanticsUpdateBridge::GetNodesPtr() const {
  return impl_ ? static_cast<const void*>(&impl_->nodes) : nullptr;
}

const void* SemanticsUpdateBridge::GetActionsPtr() const {
  return impl_ ? static_cast<const void*>(&impl_->actions) : nullptr;
}

}  // namespace flutter::swift_bridge
