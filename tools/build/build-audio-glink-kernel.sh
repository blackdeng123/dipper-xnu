#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build the experimental GLINK probe in a separate XNU source tree.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
K="$ROOT/work/hackios"
: "${THEOS:?Set THEOS to the verified local SDK/toolchain directory}"
APR=0
SAFE_REBOOT=0
if [ "${1:-}" = "--apr" ]; then APR=1; shift; fi
if [ "${1:-}" = "--safe-reboot" ]; then SAFE_REBOOT=1; shift; fi
[ "$#" -eq 1 ] || { echo "usage: $0 [--apr] [--safe-reboot] NEW-DIRECTORY-UNDER-work/runs" >&2; exit 2; }
OUT="$(realpath -m "$1")"
case "$OUT" in "$ROOT"/work/runs/*) ;; *) echo 'Output must be under work/runs' >&2; exit 2;; esac
[ ! -e "$OUT" ] || { echo 'Output already exists' >&2; exit 2; }
mkdir -p "$OUT/src"
rsync -a --exclude=/BUILD/ --exclude=/.git/ "$K/xnu-src/xnu-8796.121.2/" "$OUT/src/"
patch --directory="$OUT/src" -p1 --dry-run < "$ROOT/patches/audio-no-sink-diagnostic.patch"
patch --directory="$OUT/src" -p1 < "$ROOT/patches/audio-no-sink-diagnostic.patch"
patch --directory="$OUT/src" -p1 --dry-run < "$ROOT/patches/audio-glink-probe.patch"
patch --directory="$OUT/src" -p1 < "$ROOT/patches/audio-glink-probe.patch"
grep -Fq 'mi8rp: audio-glink: probe ended' "$OUT/src/bsd/dev/mi8_rproc.c"
if [ "$APR" -eq 1 ]; then
    clang -std=c11 -Wall -Wextra -Werror -I"$ROOT/system/kernel" \
        "$ROOT/tests/test_mi8_apr_response.c" -o "$OUT/apr-response-test"
    "$OUT/apr-response-test"
    clang -std=c11 -Wall -Wextra -Werror -I"$ROOT/system/kernel" \
        "$ROOT/tests/test_mi8_apr_client.c" -o "$OUT/apr-client-test"
    "$OUT/apr-client-test"
    clang -std=c11 -Wall -Wextra -Werror -I"$ROOT/system/kernel" \
        "$ROOT/tests/test_mi8_afe.c" -o "$OUT/afe-test"
    "$OUT/afe-test"
    clang -std=c11 -Wall -Wextra -Werror -I"$ROOT/system/kernel" \
        "$ROOT/tests/test_mi8_asm_pcm.c" -o "$OUT/pcm-test"
    "$OUT/pcm-test"
    clang -std=c11 -Wall -Wextra -Werror -pthread -I"$ROOT/system/kernel" \
        "$ROOT/tests/test_mi8_pcm_ring.c" -o "$OUT/pcm-ring-test"
    "$OUT/pcm-ring-test"
    clang -std=c11 -Wall -Wextra -Werror -I"$ROOT/system/kernel" \
        "$ROOT/tests/test_mi8_pcm_engine.c" -o "$OUT/pcm-engine-test"
    "$OUT/pcm-engine-test"
    clang -std=c11 -Wall -Wextra -Werror -I"$ROOT/system/kernel" \
        "$ROOT/tests/test_mi8_smmu_pages.c" -o "$OUT/smmu-pages-test"
    "$OUT/smmu-pages-test"
    clang -std=c11 -Wall -Wextra -Werror -I"$ROOT/system/kernel" \
        "$ROOT/tests/test_mi8_smmu_program.c" -o "$OUT/smmu-program-test"
    "$OUT/smmu-program-test"
    clang -std=c11 -Wall -Wextra -Werror -I"$ROOT/system/kernel" \
        "$ROOT/tests/test_mi8_audio_session.c" -o "$OUT/audio-session-test"
    "$OUT/audio-session-test"
    clang -std=c11 -Wall -Wextra -Werror -I"$ROOT/system/kernel" \
        "$ROOT/tests/test_mi8_i2c.c" -o "$OUT/i2c-test"
    "$OUT/i2c-test"
    clang -std=c11 -Wall -Wextra -Werror -I"$ROOT/system/kernel" \
        "$ROOT/tests/test_mi8_speaker_i2c.c" -o "$OUT/speaker-i2c-test"
    "$OUT/speaker-i2c-test"
    clang -std=c11 -Wall -Wextra -Werror -I"$ROOT/system/kernel" \
        "$ROOT/tests/test_mi8_speaker_reset.c" -o "$OUT/speaker-reset-test"
    "$OUT/speaker-reset-test"
    clang -std=c11 -Wall -Wextra -Werror -I"$ROOT/system/kernel" \
        "$ROOT/tests/test_mi8_tas_init.c" -o "$OUT/tas-init-test"
    "$OUT/tas-init-test"
    patch --directory="$OUT/src" -p1 --dry-run < "$ROOT/patches/audio-apr-probe.patch"
    patch --directory="$OUT/src" -p1 < "$ROOT/patches/audio-apr-probe.patch"
    patch --directory="$OUT/src" -p1 --dry-run < "$ROOT/patches/audio-glink-intents.patch"
    patch --directory="$OUT/src" -p1 < "$ROOT/patches/audio-glink-intents.patch"
    patch --directory="$OUT/src" -p1 --dry-run < "$ROOT/patches/audio-apr-versions.patch"
    patch --directory="$OUT/src" -p1 < "$ROOT/patches/audio-apr-versions.patch"
    cp "$ROOT/system/kernel/mi8-apr-response.h" "$OUT/src/bsd/dev/mi8-apr-response.h"
    patch --directory="$OUT/src" -p1 --dry-run < "$ROOT/patches/audio-apr-response.patch"
    patch --directory="$OUT/src" -p1 < "$ROOT/patches/audio-apr-response.patch"
    cp "$ROOT/system/kernel/mi8-apr-client.h" "$OUT/src/bsd/dev/mi8-apr-client.h"
    patch --directory="$OUT/src" -p1 --dry-run < "$ROOT/patches/audio-apr-client.patch"
    patch --directory="$OUT/src" -p1 < "$ROOT/patches/audio-apr-client.patch"
    patch --directory="$OUT/src" -p1 --dry-run < "$ROOT/patches/audio-smmu-lock.patch"
    patch --directory="$OUT/src" -p1 < "$ROOT/patches/audio-smmu-lock.patch"
    cp "$ROOT/system/kernel/mi8-afe.h" "$OUT/src/bsd/dev/mi8-afe.h"
    cp "$ROOT/system/kernel/mi8-adm.h" "$OUT/src/bsd/dev/mi8-adm.h"
    cp "$ROOT/system/kernel/mi8-asm.h" "$OUT/src/bsd/dev/mi8-asm.h"
    cp "$ROOT/system/kernel/mi8-asm-pcm.h" "$OUT/src/bsd/dev/mi8-asm-pcm.h"
    cp "$ROOT/system/kernel/mi8-pcm-ring.h" "$OUT/src/bsd/dev/mi8-pcm-ring.h"
    cp "$ROOT/system/kernel/mi8-pcm-engine.h" "$OUT/src/bsd/dev/mi8-pcm-engine.h"
    cp "$ROOT/system/kernel/mi8-audio-sink.h" "$OUT/src/bsd/dev/mi8-audio-sink.h"
    cp "$ROOT/system/kernel/mi8-audio-session.h" "$OUT/src/bsd/dev/mi8-audio-session.h"
    cp "$ROOT/system/kernel/mi8-audio-dma.inc" "$OUT/src/bsd/dev/mi8-audio-dma.inc"
    cp "$ROOT/system/kernel/mi8-audio-dma-probe.inc" "$OUT/src/bsd/dev/mi8-audio-dma-probe.inc"
    cp "$ROOT/system/kernel/mi8-i2c.h" "$OUT/src/bsd/dev/mi8-i2c.h"
    cp "$ROOT/system/kernel/mi8-speaker-i2c.inc" "$OUT/src/bsd/dev/mi8-speaker-i2c.inc"
    cp "$ROOT/system/kernel/mi8-speaker-reset.inc" "$OUT/src/bsd/dev/mi8-speaker-reset.inc"
    cp "$ROOT/system/kernel/mi8-tas-init.h" "$OUT/src/bsd/dev/mi8-tas-init.h"
    cp "$ROOT/system/kernel/mi8-speaker-init.inc" "$OUT/src/bsd/dev/mi8-speaker-init.inc"
    cp "$ROOT/system/kernel/mi8-speaker-probe.inc" "$OUT/src/bsd/dev/mi8-speaker-probe.inc"
    cp "$ROOT/system/kernel/mi8-tas-private.h" "$OUT/src/bsd/dev/mi8-tas-private.h"
    cp "$ROOT/system/kernel/mi8-smmu-pages.h" "$OUT/src/bsd/dev/mi8-smmu-pages.h"
    cp "$ROOT/system/kernel/mi8-smmu-program.h" "$OUT/src/bsd/dev/mi8-smmu-program.h"
    patch --directory="$OUT/src" -p1 --dry-run < "$ROOT/patches/audio-afe-probe.patch"
    patch --directory="$OUT/src" -p1 < "$ROOT/patches/audio-afe-probe.patch"
    clang -std=c11 -Wall -Wextra -Werror -DMI8_GLINK_HOST_TEST -I"$OUT/src/bsd/dev" \
        "$ROOT/tests/audio_glink_raw_test.c" "$OUT/src/bsd/dev/mi8_glink.c" -pthread -o "$OUT/glink-host-test"
    "$OUT/glink-host-test"
    python3 "$ROOT/tests/test_audio_apr_versions.py" "$OUT/src"
fi
if [ "$SAFE_REBOOT" -eq 1 ]; then
    patch --directory="$OUT/src" -p1 --dry-run < "$ROOT/patches/safe-reboot-kernel.patch"
    patch --directory="$OUT/src" -p1 < "$ROOT/patches/safe-reboot-kernel.patch"
    patch --directory="$OUT/src" -p1 --dry-run < "$ROOT/patches/audio-auto-recovery-kernel.patch"
    patch --directory="$OUT/src" -p1 < "$ROOT/patches/audio-auto-recovery-kernel.patch"
    python3 "$ROOT/tests/test_audio_auto_recovery.py" "$OUT/src"
fi
XNU_SRC="$OUT/src" bash "$K/xnu-build.sh" -j8 build
cp "$OUT/src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837" "$OUT/kernel.macho"
grep -aFq 'mi8rp: audio-glink: probe ended' "$OUT/kernel.macho"
if [ "$APR" -eq 1 ]; then grep -aFq 'mi8rp: audio-apr: state request' "$OUT/kernel.macho"; fi
if [ "$SAFE_REBOOT" -eq 1 ]; then grep -aFq 'automatic reboot: save confirmed' "$OUT/kernel.macho"; fi
sha256sum "$OUT/kernel.macho" > "$OUT/SHA256SUMS"
echo 'Host build only: no device operation was performed.'
