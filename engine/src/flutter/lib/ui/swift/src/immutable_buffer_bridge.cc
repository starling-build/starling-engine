// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/immutable_buffer_bridge.h"

// Flutter engine headers (only in .cc file)
#include "flutter/fml/file.h"
#include "flutter/fml/mapping.h"
#include "third_party/skia/include/core/SkData.h"

#include <cstring>

#if FML_OS_ANDROID
#include <sys/mman.h>
#endif

namespace flutter::swift_bridge {

namespace {

// Inline MakeSkDataWithCopy to match the engine's ImmutableBuffer behavior.
// On Android, uses mmap to avoid heap fragmentation issues.
// On other platforms, uses SkData::MakeWithCopy directly.
//
// **Dart Source:** `immutable_buffer.cc:192-235`
#if FML_OS_ANDROID
sk_sp<SkData> MakeSkDataWithCopy(const void* data, size_t length) {
  if (length == 0) {
    return SkData::MakeEmpty();
  }

  size_t mapping_length = length + sizeof(size_t);
  void* mapping = ::mmap(nullptr, mapping_length, PROT_READ | PROT_WRITE,
                         MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

  if (mapping == MAP_FAILED) {
    return SkData::MakeEmpty();
  }

  *reinterpret_cast<size_t*>(mapping) = mapping_length;
  void* mapping_data = reinterpret_cast<char*>(mapping) + sizeof(size_t);
  ::memcpy(mapping_data, data, length);

  SkData::ReleaseProc proc = [](const void* ptr, void* context) {
    size_t* size_ptr = reinterpret_cast<size_t*>(context);
    if (::munmap(const_cast<void*>(context), *size_ptr) == -1) {
      // munmap failed
    }
  };

  return SkData::MakeWithProc(mapping_data, length, proc, mapping);
}
#else
sk_sp<SkData> MakeSkDataWithCopy(const void* data, size_t length) {
  return SkData::MakeWithCopy(data, length);
}
#endif

}  // namespace

// Pimpl implementation holding actual Flutter/Skia types
struct ImmutableBufferBridgeImpl {
  sk_sp<SkData> data;
  bool disposed = false;

  explicit ImmutableBufferBridgeImpl(sk_sp<SkData> d)
      : data(std::move(d)) {}
};

ImmutableBufferBridge::ImmutableBufferBridge(const uint8_t* data,
                                             size_t length) {
  sk_sp<SkData> sk_data;
  if (data != nullptr && length > 0) {
    sk_data = MakeSkDataWithCopy(data, length);
  } else {
    sk_data = SkData::MakeEmpty();
  }
  impl_ = new ImmutableBufferBridgeImpl(std::move(sk_data));
}

ImmutableBufferBridge::~ImmutableBufferBridge() {
  delete impl_;
}

ImmutableBufferBridge* ImmutableBufferBridge::CreateFromAsset(
    const char* asset_key) {
  // DIFFERENCE FROM DART: The Dart version uses the asset manager from
  // UIDartState to load assets asynchronously. Since we don't have access
  // to the Dart state, this is a placeholder that creates an empty buffer.
  // The actual asset loading will need to be wired up through the Swift
  // framework's asset management system.
  //
  // For now, return an empty buffer (length 0) to indicate asset not found.
  auto* bridge = new ImmutableBufferBridge(nullptr, 0);
  return bridge;
}

ImmutableBufferBridge* ImmutableBufferBridge::CreateFromFile(
    const char* file_path) {
  // Read the file using FML file utilities
  auto fd = fml::OpenFile(file_path, false, fml::FilePermission::kRead);
  auto mapping = std::make_unique<fml::FileMapping>(fd);

  if (!mapping->IsValid() || mapping->GetSize() == 0) {
    // Return empty buffer to indicate file not found/unreadable
    auto* bridge = new ImmutableBufferBridge(nullptr, 0);
    return bridge;
  }

  size_t buffer_size = mapping->GetSize();
  const void* bytes = static_cast<const void*>(mapping->GetMapping());
  auto sk_data = MakeSkDataWithCopy(bytes, buffer_size);

  auto* impl = new ImmutableBufferBridgeImpl(std::move(sk_data));
  auto* bridge = new ImmutableBufferBridge(nullptr, 0);
  delete bridge->impl_;
  bridge->impl_ = impl;
  return bridge;
}

size_t ImmutableBufferBridge::Length() const {
  if (!impl_ || impl_->disposed || !impl_->data) {
    return 0;
  }
  return impl_->data->size();
}

void ImmutableBufferBridge::Dispose() {
  if (impl_ && !impl_->disposed) {
    impl_->data.reset();
    impl_->disposed = true;
  }
}

bool ImmutableBufferBridge::IsDisposed() const {
  return !impl_ || impl_->disposed;
}

const void* ImmutableBufferBridge::GetSkDataPtr() const {
  if (!impl_ || impl_->disposed || !impl_->data) {
    return nullptr;
  }
  return &impl_->data;
}

}  // namespace flutter::swift_bridge
