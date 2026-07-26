// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/semantics_update_builder_bridge.h"
#include "include/semantics_flags_bridge.h"
#include "include/semantics_update_bridge.h"

// Note: We cannot include flutter/lib/ui/semantics/semantics_update_builder.h
// because it pulls in Dart VM dependencies (dart_wrapper.h -> tonic).
// Instead, we replicate the SemanticsUpdateBuilder logic directly, storing
// SemanticsNode and CustomAccessibilityAction in maps.
// See: flutter/lib/ui/semantics/semantics_update_builder.cc for the
//   original implementation.

// We CAN include these engine headers since they don't depend on Dart VM
#include "third_party/skia/include/core/SkM44.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/include/core/SkScalar.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace flutter::swift_bridge {

namespace {

// Inline SafeNarrow to avoid dependency on //flutter/lib/ui:ui
inline float SafeNarrow(double value) {
  if (std::isinf(value) || std::isnan(value)) {
    return static_cast<float>(value);
  }
  return std::clamp(static_cast<float>(value),
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::max());
}

}  // namespace

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

// Helper to convert parallel arrays of string attribute data into
// StringAttributes vector.
// Matches the logic in semantics_update_builder.cc:19-25
// (pushStringAttributes) but builds from primitive arrays instead of
// NativeStringAttribute* objects.
// Uses const char* const* to avoid std::string ABI mismatch.
void BuildStringAttributes(
    StringAttributes& destination,
    int32_t count,
    const int32_t* types,
    const int32_t* starts,
    const int32_t* ends,
    const char* const* locales) {
  for (int32_t i = 0; i < count; ++i) {
    if (types[i] == 0) {
      // SpellOut attribute
      auto attr = std::make_shared<SpellOutStringAttribute>();
      attr->start = starts[i];
      attr->end = ends[i];
      attr->type = StringAttributeType::kSpellOut;
      destination.push_back(std::move(attr));
    } else if (types[i] == 1) {
      // Locale attribute
      auto attr = std::make_shared<LocaleStringAttribute>();
      attr->start = starts[i];
      attr->end = ends[i];
      attr->type = StringAttributeType::kLocale;
      if (locales && locales[i]) {
        attr->locale = locales[i];
      }
      destination.push_back(std::move(attr));
    }
  }
}

// Pimpl implementation holding the accumulated updates.
// Mirrors flutter::SemanticsUpdateBuilder private state from
// semantics_update_builder.h:83-85
struct SemanticsUpdateBuilderImpl {
  SemanticsNodeUpdates nodes;
  CustomAccessibilityActionUpdates actions;
};

SemanticsUpdateBuilderBridge::SemanticsUpdateBuilderBridge() {
  impl_ = new SemanticsUpdateBuilderImpl();
}

SemanticsUpdateBuilderBridge::~SemanticsUpdateBuilderBridge() {
  delete impl_;
}

void SemanticsUpdateBuilderBridge::UpdateNode(
    int32_t id,
    SemanticsFlagsBridge* flags_bridge,
    int32_t actions,
    int32_t max_value_length,
    int32_t current_value_length,
    int32_t text_selection_base,
    int32_t text_selection_extent,
    int32_t platform_view_id,
    int32_t scroll_children,
    int32_t scroll_index,
    double scroll_position,
    double scroll_extent_max,
    double scroll_extent_min,
    double left,
    double top,
    double right,
    double bottom,
    const char* identifier,
    const char* label,
    int32_t label_attribute_count,
    const int32_t* label_attribute_types,
    const int32_t* label_attribute_starts,
    const int32_t* label_attribute_ends,
    const char* const* label_attribute_locales,
    const char* value,
    int32_t value_attribute_count,
    const int32_t* value_attribute_types,
    const int32_t* value_attribute_starts,
    const int32_t* value_attribute_ends,
    const char* const* value_attribute_locales,
    const char* increased_value,
    int32_t increased_value_attribute_count,
    const int32_t* increased_value_attribute_types,
    const int32_t* increased_value_attribute_starts,
    const int32_t* increased_value_attribute_ends,
    const char* const* increased_value_attribute_locales,
    const char* decreased_value,
    int32_t decreased_value_attribute_count,
    const int32_t* decreased_value_attribute_types,
    const int32_t* decreased_value_attribute_starts,
    const int32_t* decreased_value_attribute_ends,
    const char* const* decreased_value_attribute_locales,
    const char* hint,
    int32_t hint_attribute_count,
    const int32_t* hint_attribute_types,
    const int32_t* hint_attribute_starts,
    const int32_t* hint_attribute_ends,
    const char* const* hint_attribute_locales,
    const char* tooltip,
    int32_t text_direction,
    const double* transform,
    int32_t transform_length,
    const int32_t* children_in_traversal_order,
    int32_t children_in_traversal_order_length,
    const int32_t* children_in_hit_test_order,
    int32_t children_in_hit_test_order_length,
    const int32_t* additional_actions,
    int32_t additional_actions_length,
    int32_t heading_level,
    const char* link_url,
    int32_t role,
    const char* controls_nodes,
    bool has_controls_nodes,
    int32_t validation_result,
    int32_t input_type,
    const char* locale) {
  // Matches semantics_update_builder.cc:33-133

  SemanticsNode node;
  node.id = id;

  // Extract flags from the bridge object
  // Matches semantics_update_builder.cc:81-83
  if (flags_bridge) {
    node.flags.isChecked =
        static_cast<SemanticsCheckState>(flags_bridge->GetIsChecked());
    node.flags.isSelected =
        static_cast<SemanticsTristate>(flags_bridge->GetIsSelected());
    node.flags.isEnabled =
        static_cast<SemanticsTristate>(flags_bridge->GetIsEnabled());
    node.flags.isToggled =
        static_cast<SemanticsTristate>(flags_bridge->GetIsToggled());
    node.flags.isExpanded =
        static_cast<SemanticsTristate>(flags_bridge->GetIsExpanded());
    node.flags.isRequired =
        static_cast<SemanticsTristate>(flags_bridge->GetIsRequired());
    node.flags.isFocused =
        static_cast<SemanticsTristate>(flags_bridge->GetIsFocused());
    node.flags.isButton = flags_bridge->GetIsButton();
    node.flags.isTextField = flags_bridge->GetIsTextField();
    node.flags.isInMutuallyExclusiveGroup =
        flags_bridge->GetIsInMutuallyExclusiveGroup();
    node.flags.isHeader = flags_bridge->GetIsHeader();
    node.flags.isObscured = flags_bridge->GetIsObscured();
    node.flags.scopesRoute = flags_bridge->GetScopesRoute();
    node.flags.namesRoute = flags_bridge->GetNamesRoute();
    node.flags.isHidden = flags_bridge->GetIsHidden();
    node.flags.isImage = flags_bridge->GetIsImage();
    node.flags.isLiveRegion = flags_bridge->GetIsLiveRegion();
    node.flags.hasImplicitScrolling = flags_bridge->GetHasImplicitScrolling();
    node.flags.isMultiline = flags_bridge->GetIsMultiline();
    node.flags.isReadOnly = flags_bridge->GetIsReadOnly();
    node.flags.isLink = flags_bridge->GetIsLink();
    node.flags.isSlider = flags_bridge->GetIsSlider();
    node.flags.isKeyboardKey = flags_bridge->GetIsKeyboardKey();
  }

  node.actions = actions;
  node.maxValueLength = max_value_length;
  node.currentValueLength = current_value_length;
  node.textSelectionBase = text_selection_base;
  node.textSelectionExtent = text_selection_extent;
  node.platformViewId = platform_view_id;
  node.scrollChildren = scroll_children;
  node.scrollIndex = scroll_index;
  node.scrollPosition = scroll_position;
  node.scrollExtentMax = scroll_extent_max;
  node.scrollExtentMin = scroll_extent_min;

  // Convert rect using SafeNarrow, matching semantics_update_builder.cc:95-96
  node.rect = SkRect::MakeLTRB(SafeNarrow(left), SafeNarrow(top),
                                SafeNarrow(right), SafeNarrow(bottom));

  node.identifier = identifier ? identifier : "";

  // Label and attributes
  node.label = label ? label : "";
  if (label_attribute_count > 0 && label_attribute_types &&
      label_attribute_starts && label_attribute_ends) {
    BuildStringAttributes(node.labelAttributes, label_attribute_count,
                          label_attribute_types, label_attribute_starts,
                          label_attribute_ends, label_attribute_locales);
  }

  // Value and attributes
  node.value = value ? value : "";
  if (value_attribute_count > 0 && value_attribute_types &&
      value_attribute_starts && value_attribute_ends) {
    BuildStringAttributes(node.valueAttributes, value_attribute_count,
                          value_attribute_types, value_attribute_starts,
                          value_attribute_ends, value_attribute_locales);
  }

  // Increased value and attributes
  node.increasedValue = increased_value ? increased_value : "";
  if (increased_value_attribute_count > 0 && increased_value_attribute_types &&
      increased_value_attribute_starts && increased_value_attribute_ends) {
    BuildStringAttributes(node.increasedValueAttributes,
                          increased_value_attribute_count,
                          increased_value_attribute_types,
                          increased_value_attribute_starts,
                          increased_value_attribute_ends,
                          increased_value_attribute_locales);
  }

  // Decreased value and attributes
  node.decreasedValue = decreased_value ? decreased_value : "";
  if (decreased_value_attribute_count > 0 && decreased_value_attribute_types &&
      decreased_value_attribute_starts && decreased_value_attribute_ends) {
    BuildStringAttributes(node.decreasedValueAttributes,
                          decreased_value_attribute_count,
                          decreased_value_attribute_types,
                          decreased_value_attribute_starts,
                          decreased_value_attribute_ends,
                          decreased_value_attribute_locales);
  }

  // Hint and attributes
  node.hint = hint ? hint : "";
  if (hint_attribute_count > 0 && hint_attribute_types &&
      hint_attribute_starts && hint_attribute_ends) {
    BuildStringAttributes(node.hintAttributes, hint_attribute_count,
                          hint_attribute_types, hint_attribute_starts,
                          hint_attribute_ends, hint_attribute_locales);
  }

  node.tooltip = tooltip ? tooltip : "";
  node.textDirection = text_direction;

  // Transform: convert 16 doubles to SkM44 via SafeNarrow
  // Matches semantics_update_builder.cc:110-114
  if (transform && transform_length == 16) {
    SkScalar scalar_transform[16];
    for (int i = 0; i < 16; ++i) {
      scalar_transform[i] = SafeNarrow(transform[i]);
    }
    node.transform = SkM44::ColMajor(scalar_transform);
  }

  // Children in traversal order
  // Matches semantics_update_builder.cc:115-118
  if (children_in_traversal_order && children_in_traversal_order_length > 0) {
    node.childrenInTraversalOrder = std::vector<int32_t>(
        children_in_traversal_order,
        children_in_traversal_order + children_in_traversal_order_length);
  }

  // Children in hit test order
  // Matches semantics_update_builder.cc:119-121
  if (children_in_hit_test_order && children_in_hit_test_order_length > 0) {
    node.childrenInHitTestOrder = std::vector<int32_t>(
        children_in_hit_test_order,
        children_in_hit_test_order + children_in_hit_test_order_length);
  }

  // Additional (custom) actions
  // Matches semantics_update_builder.cc:122-124
  if (additional_actions && additional_actions_length > 0) {
    node.customAccessibilityActions = std::vector<int32_t>(
        additional_actions,
        additional_actions + additional_actions_length);
  }

  node.headingLevel = heading_level;
  node.linkUrl = link_url ? link_url : "";
  node.role = static_cast<SemanticsRole>(role);
  node.validationResult =
      static_cast<SemanticsValidationResult>(validation_result);
  node.locale = locale ? locale : "";

  // Store the node, matching semantics_update_builder.cc:132
  impl_->nodes[id] = std::move(node);
}

void SemanticsUpdateBuilderBridge::UpdateCustomAction(int32_t id,
                                                       const char* label,
                                                       const char* hint,
                                                       int32_t override_id) {
  // Matches semantics_update_builder.cc:135-145
  CustomAccessibilityAction action;
  action.id = id;
  action.overrideId = override_id;
  action.label = label ? label : "";
  action.hint = hint ? hint : "";
  impl_->actions[id] = std::move(action);
}

SemanticsUpdateBridge* SemanticsUpdateBuilderBridge::Build() {
  // Matches semantics_update_builder.cc:147-151
  // Move the accumulated nodes and actions into a new SemanticsUpdateBridge.
  auto* result = new SemanticsUpdateBridge(
      static_cast<void*>(&impl_->nodes),
      static_cast<void*>(&impl_->actions));
  // After build, clear the builder (nodes/actions were moved)
  return result;
}

}  // namespace flutter::swift_bridge
