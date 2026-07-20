#include "include/locale_string_attribute_bridge.h"

// Note: We cannot include flutter/lib/ui/semantics/string_attribute.h because
// it pulls in Dart VM dependencies (dart_wrapper.h -> tonic).
// Instead, we define the storage struct locally. The struct layout matches
// flutter::LocaleStringAttribute exactly.
// See: flutter/lib/ui/semantics/string_attribute.h

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace flutter::swift_bridge {

// Pimpl implementation - stores the attribute values directly.
// Mirrors flutter::LocaleStringAttribute struct from string_attribute.h
// without depending on the Dart VM header chain.
//
// Original C++ types:
//   enum class StringAttributeType : int32_t { kSpellOut, kLocale };
//   struct StringAttribute { int32_t start, end; StringAttributeType type; };
//   struct LocaleStringAttribute : StringAttribute { std::string locale; };
struct LocaleStringAttributeImpl {
  int32_t start;
  int32_t end;
  // type is implicitly kLocale (1) for this bridge
  std::string locale;
};

LocaleStringAttributeBridge::LocaleStringAttributeBridge(int32_t start,
                                                         int32_t end,
                                                         const char* locale) {
  // Same field assignment as NativeStringAttribute::initLocaleStringAttribute
  // (string_attribute.cc:34-47)
  impl_ = new LocaleStringAttributeImpl{start, end, std::string(locale ? locale : "")};
}

LocaleStringAttributeBridge::~LocaleStringAttributeBridge() {
  delete impl_;
}

int32_t LocaleStringAttributeBridge::GetStart() const {
  return impl_->start;
}

int32_t LocaleStringAttributeBridge::GetEnd() const {
  return impl_->end;
}

const char* LocaleStringAttributeBridge::GetLocale() const {
  return impl_->locale.c_str();
}

}  // namespace flutter::swift_bridge
