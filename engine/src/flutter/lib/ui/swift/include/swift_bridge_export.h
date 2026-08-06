// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_BRIDGE_EXPORT_H_
#define FLUTTER_SWIFT_BRIDGE_EXPORT_H_

// Export annotation for the bridge classes the Swift package binds to.
//
// On ELF and Mach-O the engine library exports these by symbol pattern —
// embedder_exports.lst lists the whole flutter::swift_bridge namespace — so
// the declarations only need default visibility, which is what they carried
// literally (a visibility("default") attribute) before this macro.
//
// Windows has no version-script equivalent: a symbol leaves a DLL only if its
// declaration says so. The engine build defines
// FLUTTER_SWIFT_BRIDGE_IMPLEMENTATION and exports; a consumer compiling
// against the vendored headers gets dllimport only when it opts in with
// FLUTTER_SWIFT_BRIDGE_DLL, and otherwise nothing at all — because the same
// headers must also serve a build that links the engine statically, where
// dllimport would be a link error rather than an optimisation.
#if defined(_WIN32)
#if defined(FLUTTER_SWIFT_BRIDGE_IMPLEMENTATION)
#define FLUTTER_SWIFT_BRIDGE_EXPORT __declspec(dllexport)
#elif defined(FLUTTER_SWIFT_BRIDGE_DLL)
#define FLUTTER_SWIFT_BRIDGE_EXPORT __declspec(dllimport)
#else
#define FLUTTER_SWIFT_BRIDGE_EXPORT
#endif
#else
#define FLUTTER_SWIFT_BRIDGE_EXPORT __attribute__((visibility("default")))
#endif

#endif  // FLUTTER_SWIFT_BRIDGE_EXPORT_H_
