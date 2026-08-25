// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/fml/logging.h"
#ifdef FLUTTER_NO_DART_VM
// There are no isolates to scope to. Dart_Isolate is an opaque handle in
// dart_api.h; keeping the shape means the callers that hold an Isolate --
// plugin registrars, mostly -- compile either way.
using Dart_Isolate = void*;
#else
#include "third_party/dart/runtime/include/dart_api.h"
#endif

#ifndef FLUTTER_SHELL_PLATFORM_COMMON_ISOLATE_SCOPE_H_
#define FLUTTER_SHELL_PLATFORM_COMMON_ISOLATE_SCOPE_H_

namespace flutter {

/// This class is a thin wrapper around dart isolate. It can be used
/// as argument to IsolateScope constructor to enter and exit the isolate.
class Isolate {
 public:
  /// Retrieve the current Dart Isolate. If no isolate is current, this
  /// results in a crash.
  static Isolate Current();

  ~Isolate() = default;

 private:
  explicit Isolate(Dart_Isolate isolate) : isolate_(isolate) {
    FML_DCHECK(isolate_ != nullptr);
  }

  friend class IsolateScope;
  Dart_Isolate isolate_;
};

// Enters provided isolate for as long as the scope is alive.
class IsolateScope {
 public:
  explicit IsolateScope(const Isolate& isolate);
  ~IsolateScope();

 private:
  Dart_Isolate isolate_;
  Dart_Isolate previous_;

  FML_DISALLOW_COPY_AND_ASSIGN(IsolateScope);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_COMMON_ISOLATE_SCOPE_H_
