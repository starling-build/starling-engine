#!/usr/bin/env bash
# Copyright the Starling authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#
# Re-apply local Skia / engine patches under engine/src.
#
# The Flutter engine fetches third_party/skia via gclient sync; that overwrites
# any local edits. Patches in ../patches/skia-*.patch are unified diffs rooted
# at engine/src/. Run this after `gclient sync` (or whenever third_party is
# refreshed) before building the engine.
#
# Each patch is applied idempotently — if it's already applied (or partially
# applied), we skip it instead of corrupting the tree.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE_SRC="${ROOT}/engine/src"
PATCH_DIR="${ROOT}/patches"

if [ ! -d "${ENGINE_SRC}" ]; then
    echo "error: engine source not found at ${ENGINE_SRC}" >&2
    exit 1
fi

if ! compgen -G "${PATCH_DIR}/skia-*.patch" > /dev/null; then
    echo "no skia patches found in ${PATCH_DIR}"
    exit 0
fi

applied=0
skipped=0
failed=0

for patch_file in "${PATCH_DIR}"/skia-*.patch; do
    name=$(basename "${patch_file}")

    # Forward dry-run; if it fails with "previously applied", treat as skip.
    fwd_out=$(patch --dry-run -p1 -d "${ENGINE_SRC}" -i "${patch_file}" 2>&1 || true)

    if grep -q "previously applied" <<< "${fwd_out}"; then
        echo "  [skip] ${name} (already applied)"
        skipped=$((skipped + 1))
        continue
    fi

    if ! grep -q "checking file" <<< "${fwd_out}"; then
        echo "  [fail] ${name}" >&2
        echo "${fwd_out}" >&2
        failed=$((failed + 1))
        continue
    fi

    if patch -p1 -d "${ENGINE_SRC}" -i "${patch_file}"; then
        echo "  [ok]   ${name}"
        applied=$((applied + 1))
    else
        echo "  [fail] ${name}" >&2
        failed=$((failed + 1))
    fi
done

echo "applied=${applied} skipped=${skipped} failed=${failed}"
[ "${failed}" -eq 0 ]
