#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build the save-and-reboot kernel in a separate XNU tree; no device access.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
K="$ROOT/work/hackios"
: "${THEOS:?Set THEOS to the verified local SDK/toolchain directory}"
[ "$#" -eq 1 ] || { echo 'usage: build-safe-reboot-kernel.sh NEW-DIRECTORY-UNDER-work/runs' >&2; exit 2; }
OUT="$(realpath -m "$1")"
case "$OUT" in "$ROOT"/work/runs/*) ;; *) echo 'Output must be under work/runs' >&2; exit 2;; esac
[ ! -e "$OUT" ] || { echo 'Output already exists' >&2; exit 2; }
mkdir -p "$OUT/src"
rsync -a --exclude=/BUILD/ --exclude=/.git/ "$K/xnu-src/xnu-8796.121.2/" "$OUT/src/"
patch --directory="$OUT/src" -p1 --dry-run < "$ROOT/patches/safe-reboot-kernel.patch"
patch --directory="$OUT/src" -p1 < "$ROOT/patches/safe-reboot-kernel.patch"
XNU_SRC="$OUT/src" bash "$K/xnu-build.sh" -j8 build
cp "$OUT/src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837" "$OUT/kernel.macho"
grep -aFq 'automatic reboot: save confirmed' "$OUT/kernel.macho"
sha256sum "$OUT/kernel.macho" > "$OUT/SHA256SUMS"
