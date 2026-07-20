#include "include/spell_out_string_attribute_bridge.h"

// Note: We cannot include flutter/lib/ui/semantics/string_attribute.h because
// it pulls in Dart VM dependencies (dart_wrapper.h -> tonic).
// Instead, we define the storage struct locally. The struct layout matches
// flutter::SpellOutStringAttribute exactly.
// See: flutter/lib/ui/semantics/string_attribute.h

#include <cstdint>
#include <memory>

namespace flutter::swift_bridge {

// Pimpl implementation - stores the attribute values directly.
// Mirrors flutter::SpellOutStringAttribute struct from string_attribute.h
// without depending on the Dart VM header chain.
//
// Original C++ types:
//   enum class StringAttributeType : int32_t { kSpellOut, kLocale };
//   struct StringAttribute { int32_t start, end; StringAttributeType type; };
//   struct SpellOutStringAttribute : StringAttribute {};
struct SpellOutStringAttributeImpl {
  int32_t start;
  int32_t end;
  // type is implicitly kSpellOut (0) for this bridge
};

SpellOutStringAttributeBridge::SpellOutStringAttributeBridge(int32_t start,
                                                             int32_t end) {
  // Same field assignment as NativeStringAttribute::initSpellOutStringAttribute
  // (string_attribute.cc:23-32)
  impl_ = new SpellOutStringAttributeImpl{start, end};
}

SpellOutStringAttributeBridge::~SpellOutStringAttributeBridge() {
  delete impl_;
}

int32_t SpellOutStringAttributeBridge::GetStart() const {
  return impl_->start;
}

int32_t SpellOutStringAttributeBridge::GetEnd() const {
  return impl_->end;
}

}  // namespace flutter::swift_bridge
