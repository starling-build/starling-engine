#!/usr/bin/env python3
#
# Copyright 2013 The Flutter Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Copies the Info.plist and adds extra fields to it like the git hash of the
engine.

usage: copy_info_plist.py --source <src_path> --destination <dest_path>
                          --minversion=<deployment_target>
                          --buildmode=<build_mode>
"""

import argparse
import os
import subprocess

import git_revision

_script_dir = os.path.abspath(os.path.join(os.path.realpath(__file__), '..'))
_src_root_dir = os.path.join(_script_dir, '..', '..')


def get_clang_version():
  # The toolchain that can run here: an Apple Silicon checkout has both
  # mac-arm64 and mac-x64, and without Rosetta the x64 clang cannot be
  # executed at all ("Bad CPU type in executable").
  import platform
  arches = ['mac-arm64', 'mac-x64'] if platform.machine() == 'arm64' else ['mac-x64', 'mac-arm64']
  for arch in arches:
    clang_executable = str(
        os.path.join(_src_root_dir, 'flutter', 'buildtools', arch, 'clang', 'bin', 'clang++')
    )
    if not os.path.exists(clang_executable):
      continue
    try:
      version = subprocess.check_output([clang_executable, '--version'])
      return version.splitlines()[0]
    except OSError:
      continue
  return b'clang (version unknown)'


def main():

  parser = argparse.ArgumentParser(
      description='Copies the Info.plist and adds extra fields to it like the '
      'git hash of the engine'
  )

  parser.add_argument(
      '--source', help='Path to Info.plist source template', type=str, required=True
  )
  parser.add_argument(
      '--destination', help='Path to destination Info.plist', type=str, required=True
  )
  parser.add_argument('--minversion', help='Minimum device OS version like "9.0"', type=str)
  parser.add_argument('--buildmode', help='Build Mode like Debug, Profile, Release', type=str)

  args = parser.parse_args()

  text = open(args.source).read()
  engine_path = os.path.join(_src_root_dir, 'flutter')
  revision = git_revision.get_repository_version(engine_path)
  clang_version = get_clang_version()
  text = text.format(
      revision=revision,
      clang_version=clang_version,
      min_version=args.minversion,
      build_mode=args.buildmode
  )

  with open(args.destination, 'w') as outfile:
    outfile.write(text)


if __name__ == '__main__':
  main()
