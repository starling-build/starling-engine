// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_IMMUTABLE_BUFFER_BRIDGE_H_
#define FLUTTER_SWIFT_IMMUTABLE_BUFFER_BRIDGE_H_

#include <swift/bridging>
#include "intrusive_reference_counted.h"

#include <cstddef>
#include <cstdint>

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class ImmutableBufferBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainImmutableBufferBridge(
    flutter::swift_bridge::ImmutableBufferBridge* p) noexcept;
void ReleaseImmutableBufferBridge(
    flutter::swift_bridge::ImmutableBufferBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declarations for pimpl
struct ImmutableBufferBridgeImpl;

/// C++ bridge wrapping Flutter's ImmutableBuffer.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/painting.dart:7701-7801`
/// **Original:** `ImmutableBuffer` class (read-only byte buffer managed by engine)
///
/// Architecture:
/// - Swift ImmutableBuffer wraps ImmutableBufferBridge
/// - ImmutableBufferBridge wraps sk_sp<SkData> (Skia immutable data)
/// - Swift ARC handles lifetime via SWIFT_SHARED_REFERENCE
///
/// DIFFERENCE FROM DART: The Dart version uses async factory methods with
/// Dart VM callbacks (_futurize pattern). The Swift bridge provides synchronous
/// creation methods since Swift has its own async/await mechanism. The caller
/// (Swift ImmutableBuffer) can wrap these in Swift async if needed.
class __attribute__((visibility("default")))
    SWIFT_SHARED_REFERENCE(RetainImmutableBufferBridge,
                           ReleaseImmutableBufferBridge)
        ImmutableBufferBridge
    : public IntrusiveReferenceCounted<ImmutableBufferBridge> {
 public:
  /// Creates an ImmutableBufferBridge by copying the given byte data.
  ///
  /// **Dart Source:** `painting.dart:7710-7715`
  /// **Original:** `static Future<ImmutableBuffer> fromUint8List(Uint8List list)`
  ///
  /// DIFFERENCE FROM DART: Synchronous instead of async. The Dart version
  /// uses _futurize pattern with callbacks; Swift can wrap in async if needed.
  ///
  /// @param data Pointer to the byte data to copy
  /// @param length Number of bytes to copy
  ImmutableBufferBridge(const uint8_t* data, size_t length);

  /// Creates an ImmutableBufferBridge from an asset key.
  ///
  /// **Dart Source:** `painting.dart:7720-7735`
  /// **Original:** `static Future<ImmutableBuffer> fromAsset(String assetKey)`
  ///
  /// DIFFERENCE FROM DART: Synchronous. Returns an empty buffer if asset
  /// not found (caller checks length). The Dart version throws Exception
  /// asynchronously if asset not found.
  ///
  /// @param asset_key Null-terminated asset key string
  /// @return A new ImmutableBufferBridge, or one with length 0 if not found
  static ImmutableBufferBridge* CreateFromAsset(const char* asset_key);

  /// Creates an ImmutableBufferBridge from a file path.
  ///
  /// **Dart Source:** `painting.dart:7740-7750`
  /// **Original:** `static Future<ImmutableBuffer> fromFilePath(String path)`
  ///
  /// DIFFERENCE FROM DART: Synchronous. Returns an empty buffer if file
  /// not found (caller checks length). The Dart version throws Exception
  /// asynchronously if file not found.
  ///
  /// @param file_path Null-terminated file path string
  /// @return A new ImmutableBufferBridge, or one with length 0 if not found
  static ImmutableBufferBridge* CreateFromFile(const char* file_path);

  /// The length, in bytes, of the underlying data.
  ///
  /// **Dart Source:** `painting.dart:7761-7763`
  /// **Original:** `int get length => _length;`
  size_t Length() const;

  /// Release the resources used by this buffer.
  ///
  /// **Dart Source:** `painting.dart:7788-7795`
  /// **Original:** `void dispose()` + `@Native void _dispose()`
  ///
  /// After calling this method, the buffer data is released and
  /// Length() will return 0.
  void Dispose();

  /// Whether dispose() has been called.
  bool IsDisposed() const;

  /// Returns an opaque pointer to the underlying sk_sp<SkData>.
  ///
  /// This is used by other bridge classes (e.g., ImageDescriptorBridge)
  /// that need access to the underlying data. Returns nullptr if disposed.
  ///
  /// The returned pointer is valid only for the lifetime of this bridge.
  const void* GetSkDataPtr() const;

  ~ImmutableBufferBridge();

 private:
  ImmutableBufferBridge(const ImmutableBufferBridge&) = delete;
  ImmutableBufferBridge& operator=(const ImmutableBufferBridge&) = delete;

  // Pimpl - holds the actual Flutter types (sk_sp<SkData>)
  ImmutableBufferBridgeImpl* impl_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainImmutableBufferBridge(
    flutter::swift_bridge::ImmutableBufferBridge* p) noexcept {
  p->Retain();
}
inline void ReleaseImmutableBufferBridge(
    flutter::swift_bridge::ImmutableBufferBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_IMMUTABLE_BUFFER_BRIDGE_H_
