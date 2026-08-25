// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/runtime/runtime_controller_interface.h"

#ifdef FLUTTER_NO_DART_VM
#include "flutter/runtime/dart_stubs.h"
#else
#include "flutter/runtime/isolate_configuration.h"
#endif

namespace flutter {

bool RuntimeControllerInterface::LaunchRootIsolate(
    const Settings& settings,
    const fml::closure& root_isolate_create_callback,
    std::optional<std::string> dart_entrypoint,
    std::optional<std::string> dart_entrypoint_library,
    const std::vector<std::string>& dart_entrypoint_args,
    std::unique_ptr<IsolateConfiguration> isolate_configuration,
    std::shared_ptr<NativeAssetsManager> native_assets_manager,
    std::optional<int64_t> engine_id) {
  return false;
}

void RuntimeControllerInterface::LoadDartDeferredLibrary(
    intptr_t loading_unit_id,
    std::unique_ptr<const fml::Mapping> snapshot_data,
    std::unique_ptr<const fml::Mapping> snapshot_instructions) {
  // No-op default implementation.
}

}  // namespace flutter
