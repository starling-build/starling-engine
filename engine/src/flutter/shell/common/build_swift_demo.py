#!/usr/bin/env python3
# Copyright the Starling authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Builds SwiftRuntime and FlutterSwiftBridge into a static library for the blue screen demo."""

import glob
import os
import shutil
import subprocess
import sys


def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <output_lib> <output_header> <swift_package_dir>",
              file=sys.stderr)
        sys.exit(1)

    output_lib = sys.argv[1]
    output_header = sys.argv[2]
    swift_package_dir = sys.argv[3]

    toolchain_path = os.path.expanduser(
        "~/Library/Developer/Toolchains/swift-6.2.1-RELEASE.xctoolchain"
    )
    if not os.path.isdir(toolchain_path):
        print(f"Error: Swift toolchain not found at {toolchain_path}", file=sys.stderr)
        sys.exit(1)

    env = os.environ.copy()
    env["TOOLCHAINS"] = "swift-6.2.1-RELEASE"

    # Step 1: Build SwiftRuntime via SwiftPM
    # Note: SwiftRuntimeCxx (C++ pimpl) is compiled by GN instead, since it
    # needs transitive engine include paths (Skia, tonic, etc.) that GN provides.
    print("[build_swift_demo] Building SwiftRuntime...")
    subprocess.check_call(
        ["swift", "build", "--target", "SwiftRuntime"],
        cwd=swift_package_dir,
        env=env,
    )

    # Step 2: Collect .o files
    build_dir = os.path.join(swift_package_dir, ".build", "arm64-apple-macosx", "debug")

    swift_runtime_objs = glob.glob(
        os.path.join(build_dir, "SwiftRuntime.build", "*.swift.o")
    )
    flutter_bridge_objs = glob.glob(
        os.path.join(build_dir, "FlutterSwiftBridge.build", "*.swift.o")
    )

    all_objs = swift_runtime_objs + flutter_bridge_objs
    if not all_objs:
        print("Error: No .o files found after Swift build", file=sys.stderr)
        sys.exit(1)

    print(f"[build_swift_demo] Found {len(all_objs)} object files")

    # Step 3: Create static library
    os.makedirs(os.path.dirname(output_lib), exist_ok=True)
    subprocess.check_call(["ar", "rcs", output_lib] + all_objs)
    print(f"[build_swift_demo] Created {output_lib}")

    # Step 4: Copy generated header
    header_src = os.path.join(
        build_dir, "SwiftRuntime.build", "include", "SwiftRuntime-Swift.h"
    )
    if not os.path.isfile(header_src):
        print(f"Error: Generated header not found at {header_src}", file=sys.stderr)
        sys.exit(1)

    os.makedirs(os.path.dirname(output_header), exist_ok=True)
    shutil.copy2(header_src, output_header)
    print(f"[build_swift_demo] Copied header to {output_header}")

    print("[build_swift_demo] Done!")


if __name__ == "__main__":
    main()
