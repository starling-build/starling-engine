// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_INTRUSIVE_REFERENCE_COUNTED_H_
#define FLUTTER_SWIFT_INTRUSIVE_REFERENCE_COUNTED_H_

#include <atomic>

namespace flutter::swift_bridge {

/// A base class for reference-counted objects that can be shared with Swift.
///
/// This class provides intrusive reference counting that is compatible with
/// Swift's SWIFT_SHARED_REFERENCE annotation. Objects inheriting from this
/// class can be automatically managed by Swift's ARC.
///
/// Usage:
/// 1. Inherit from IntrusiveReferenceCounted<YourClass>
/// 2. Add SWIFT_SHARED_REFERENCE(Retain, Release) annotation to your class
/// 3. Swift ARC will automatically call Retain/Release
///
/// Key features:
/// - Starts with ref_count=1 (caller owns initial reference)
/// - Thread-safe atomic operations
/// - CRTP pattern for type-safe deletion
template <typename T>
class IntrusiveReferenceCounted {
 public:
  IntrusiveReferenceCounted() : ref_count_(1) {}

  /// Increments the reference count.
  /// Called by Swift ARC when a new strong reference is created.
  void Retain() {
    ref_count_.fetch_add(1, std::memory_order_relaxed);
  }

  /// Decrements the reference count and deletes the object when it reaches 0.
  /// Called by Swift ARC when a strong reference is released.
  void Release() {
    if (ref_count_.fetch_sub(1, std::memory_order_release) == 1) {
      std::atomic_thread_fence(std::memory_order_acquire);
      delete static_cast<T*>(this);
    }
  }

 protected:
  // Protected destructor ensures deletion only through Release()
  ~IntrusiveReferenceCounted() = default;

 private:
  std::atomic<int> ref_count_;
};

}  // namespace flutter::swift_bridge

#endif  // FLUTTER_SWIFT_INTRUSIVE_REFERENCE_COUNTED_H_
