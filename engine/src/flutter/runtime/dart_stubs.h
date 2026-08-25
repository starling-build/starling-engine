// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_RUNTIME_DART_STUBS_H_
#define FLUTTER_RUNTIME_DART_STUBS_H_

// Stand-ins for the Dart-owned types that survive in signatures when the VM
// is not built (flutter_enable_dart = false).
//
// RuntimeControllerInterface::LaunchRootIsolate takes a
// std::unique_ptr<IsolateConfiguration> BY VALUE, so the parameter is
// destroyed at the end of the function and the type has to be COMPLETE --
// a forward declaration compiles the header and fails the definition. In a
// build with no VM the argument is always null and nothing is ever
// constructed; these exist so the interface keeps one shape across both
// builds rather than growing a second signature nobody can implement twice.

#ifdef FLUTTER_NO_DART_VM

// NativeAssetsManager is NOT stubbed: it lives in //flutter/assets and has no
// Dart in it, so the real one is used and a stub here would be a redefinition.
#include "flutter/assets/native_assets.h"

namespace flutter {

class IsolateConfiguration {};

}  // namespace flutter

// RuntimeControllerInterface reports its state in two Dart-owned vocabulary
// types. Neither carries behaviour -- the Swift controller returns "no port"
// and "no error" for the life of the process -- so with no VM they are a
// typedef and an enum rather than a reason to keep dart_api.h and tonic.
using Dart_Port = int64_t;

#ifndef ILLEGAL_PORT
#define ILLEGAL_PORT 0
#endif

namespace tonic {

enum DartErrorHandleType {
  kNoError,
  kUnknownErrorType,
  kApiErrorType,
  kCompilationErrorType,
};

}  // namespace tonic

#endif  // FLUTTER_NO_DART_VM
#endif  // FLUTTER_RUNTIME_DART_STUBS_H_
