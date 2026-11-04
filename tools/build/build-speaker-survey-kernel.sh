#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build an isolated read-only speaker survey with automatic recovery return.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
K="$ROOT/work/hackios"
: "${THEOS:?Set THEOS to the verified local SDK/toolchain directory}"
[ "$#" -eq 1 ] || { echo "usage: $0 NEW-DIRECTORY-UNDER-work/runs" >&2; exit 2; }
OUT="$(realpath -m "$1")"
case "$OUT" in "$ROOT"/work/runs/*) ;; *) echo 'Output must be under work/runs' >&2; exit 2;; esac
[ ! -e "$OUT" ] || { echo 'Output already exists' >&2; exit 2; }
mkdir -p "$OUT/src"
rsync -a --exclude=/BUILD/ --exclude=/.git/ "$K/xnu-src/xnu-8796.121.2/" "$OUT/src/"
patch --directory="$OUT/src" -p1 --dry-run < "$ROOT/patches/safe-reboot-kernel.patch"
patch --directory="$OUT/src" -p1 < "$ROOT/patches/safe-reboot-kernel.patch"
cp "$ROOT/system/kernel/mi8-speaker-survey.inc" "$OUT/src/bsd/dev/mi8-speaker-survey.inc"
python3 - "$OUT/src/bsd/dev/mi8_fts.c" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
s = p.read_text()
anchor = 'static void\nfts_thread(void *parameter, __unused wait_result_t result)'
assert s.count(anchor) == 1
s = s.replace(anchor, '#include "mi8-speaker-survey.inc"\n\n' + anchor)
anchor = '\tgcc = ml_io_map(GCC_PHYS, GCC_SIZE);\n\tvotes = rd(gcc + GCC_VOTE_APPS);'
assert s.count(anchor) == 1
s = s.replace(anchor, '\tgcc = ml_io_map(GCC_PHYS, GCC_SIZE);\n\tmi8_speaker_survey();\n\tvotes = rd(gcc + GCC_VOTE_APPS);')
p.write_text(s)
PY
XNU_SRC="$OUT/src" bash "$K/xnu-build.sh" -j8 build
cp "$OUT/src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837" "$OUT/kernel.macho"
grep -aFq 'automatic reboot: save confirmed' "$OUT/kernel.macho"
grep -aFq 'mi8speaker: se5 skipped' "$OUT/kernel.macho"
sha256sum "$OUT/kernel.macho" > "$OUT/SHA256SUMS"
