#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build a local, experimental RAM-boot image with persistent user state.

This tool performs no device operations. The caller must independently verify
the target's GPT, firmware, recovery path, and saved data before using it.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import struct
import subprocess
import uuid
import zlib

ROOT = Path(__file__).resolve().parents[2]
KERNEL = ROOT / "work/hackios"
MIN_SYSTEM_BLOCKS = 2042368
DARWIN_TYPE = uuid.UUID("7c3457ef-0000-11aa-aa11-00306543ecac")


def run(*command, cwd=ROOT, env=None):
    subprocess.run(command, cwd=cwd, env=env, check=True)


def number(source, name):
    match = re.search(r"^#define\s+" + re.escape(name) + r"\s+(0x[0-9a-fA-F]+|[0-9]+)", source, re.M)
    if not match:
        raise ValueError("Missing constant: " + name)
    return int(match.group(1), 0)


def guid_initializer(source, name):
    match = re.search(r"^#define\s+" + re.escape(name) + r"\s+([^\n]+)", source, re.M)
    if not match:
        raise ValueError("Missing loader partition GUID: " + name)
    initializer = match.group(1).split("/*", 1)[0]
    return [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", initializer)]


def guid_values(value):
    return [value.time_low, value.time_mid, value.time_hi_version, *value.bytes[8:]]


def check_layout(guid_file):
    loader = (KERNEL / "xnuloader/loader.c").read_text()
    ufs = (KERNEL / "xnu-src/xnu-8796.121.2/bsd/dev/redwood_ufs.c").read_text()
    store = (KERNEL / "userland/src/var_store.c").read_text()
    first = number(loader, "BLOCK_DARWIN_FIRST")
    last = number(loader, "BLOCK_DARWIN_LAST")
    protect = number(loader, "BLOCK_WRITE_FIRST")
    if (last < first or first != number(ufs, "UFS_DARWIN_FIRST")
            or last - first + 1 != number(ufs, "UFS_DARWIN_BLOCKS")
            or protect != number(ufs, "UFS_WRITE_FIRST") or protect < MIN_SYSTEM_BLOCKS):
        raise ValueError("Loader and kernel partition bounds do not match")
    store_at = number(store, "STORE_AT")
    store_end = store_at + number(store, "SLOTS") * number(store, "SLOT_BYTES")
    if store_at < protect * 4096 or store_end > (last - first + 1) * 4096:
        raise ValueError("User-state store is outside the permitted partition range")
    if guid_initializer(loader, "BLOCK_DARWIN_TYPE") != guid_values(DARWIN_TYPE):
        raise ValueError("Unexpected darwin partition type GUID")
    expected = uuid.UUID(guid_file.read_text().strip())
    if expected.int == 0 or guid_initializer(loader, "BLOCK_DARWIN_UNIQ") != guid_values(expected):
        raise ValueError("Loader partition GUID differs from the local GUID file")


def apply_loader_patch():
    patch = ROOT / "patches/autoboot-loader.patch"
    base = ["git", "apply"]
    if subprocess.run(base + ["--reverse", "--check", str(patch)], cwd=KERNEL,
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
        return
    run(*base, "--check", str(patch), cwd=KERNEL)
    run(*base, str(patch), cwd=KERNEL)


def install_save_config():
    target = KERNEL / "userland/com.redwood.varstore.plist"
    original = ROOT / "work/userland/userland/com.redwood.varstore.plist"
    desired = ROOT / "system/userland/com.redwood.varstore.persistent.plist"
    current = plistlib.loads(target.read_bytes())
    if current not in (plistlib.loads(original.read_bytes()), plistlib.loads(desired.read_bytes())):
        raise ValueError("Existing varstore configuration has unrelated changes")
    shutil.copy2(desired, target)


def verify_image(out, expected_payload):
    image = (out / "boot-persistent.img").read_bytes()
    if image[:8] != b"ANDROID!":
        raise ValueError("Invalid Android boot image")
    kernel_size = struct.unpack_from("<I", image, 8)[0]
    page_size = struct.unpack_from("<I", image, 36)[0]
    if page_size < 2048 or page_size + kernel_size > len(image):
        raise ValueError("Invalid boot image kernel range")
    decoder = zlib.decompressobj(31)
    raw = decoder.decompress(image[page_size:page_size + kernel_size]) + decoder.flush()
    shim = (ROOT / "work/edk2/tools/BootShim/BootShim.bin").read_bytes()
    fd = (ROOT / "work/edk2/workspace/Build/dipper/RELEASE_CLANG38/FV/SDM845_UEFI.fd").read_bytes()
    if not decoder.eof or raw != shim + fd + expected_payload:
        raise ValueError("Boot image does not contain the expected firmware and payload")
    digest = hashlib.sha256(image).hexdigest()
    (out / "SHA256SUMS.autoboot").write_text(digest + "  boot-persistent.img\n")
    print("Verified image SHA-256:", digest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="new directory under work/runs")
    parser.add_argument("--guid-file", type=Path, required=True, help="local, verified darwin GUID file")
    features = parser.add_mutually_exclusive_group()
    features.add_argument("--wifi", action="store_true", help="enable the dipper WCN3990 station path")
    features.add_argument("--audio-probe", action="store_true", help="run Mi 8 remote-processor diagnostic steps 1–5 without Wi-Fi")
    features.add_argument("--audio-start-test", action="store_true", help="run Mi 8 audio DSP start/stop diagnostic steps 1–8 without Wi-Fi")
    features.add_argument("--audio-glink-test", action="store_true", help="probe the audio DSP communication channel without Wi-Fi")
    features.add_argument("--audio-apr-test", action="store_true", help="query audio DSP service state over APR without Wi-Fi")
    features.add_argument("--audio-afe-test", action="store_true", help="probe AFE, ADM and ASM playback setup with amplifier off and no samples")
    features.add_argument("--audio-dma-test", action="store_true", help="submit one silent DSP buffer with the amplifier off")
    features.add_argument("--auto-recovery-test", action="store_true", help="test timed save and reboot without the audio DMA probe")
    features.add_argument("--tas-init-test", action="store_true", help="test the device-private unpowered TAS2557 initialization")
    parser.add_argument("--kernel-file", type=Path, help="use a separately built kernel")
    parser.add_argument("--safe-reboot", action="store_true", help="include experimental save-and-reboot support")
    parser.add_argument("--preferences-debug", action="store_true", help="trace fatal signals in Settings using the existing app debug library")
    parser.add_argument("--preferences-icon-fix", action="store_true", help="try the Settings icon workaround and enable crash tracing")
    args = parser.parse_args()
    if args.safe_reboot and (args.kernel_file is None or not args.kernel_file.is_file() or
                             b"automatic reboot: save confirmed" not in args.kernel_file.read_bytes()):
        parser.error("--safe-reboot requires a kernel built with the save-and-reboot patch")
    if args.audio_glink_test and (args.kernel_file is None or not args.kernel_file.is_file() or
                                  b"mi8rp: audio-glink: probe ended" not in args.kernel_file.read_bytes()):
        parser.error("--audio-glink-test requires a kernel built with the audio GLINK probe")
    if args.audio_apr_test and (args.kernel_file is None or not args.kernel_file.is_file() or
                                b"mi8rp: audio-apr: state request" not in args.kernel_file.read_bytes()):
        parser.error("--audio-apr-test requires a kernel built with the audio APR probe")
    if args.audio_afe_test and (args.kernel_file is None or not args.kernel_file.is_file() or
                                b"mi8rp: audio-afe: quat" not in args.kernel_file.read_bytes()):
        parser.error("--audio-afe-test requires a kernel built with the audio AFE probe")
    if args.audio_dma_test and (args.kernel_file is None or not args.kernel_file.is_file() or
                                b"mi8rp: audio-dma: sending one silent buffer" not in args.kernel_file.read_bytes()):
        parser.error("--audio-dma-test requires a kernel built with the audio DMA probe")
    if (args.audio_dma_test or args.auto_recovery_test or args.tas_init_test) and (not args.safe_reboot or
                                b"automatic recovery: timed save and reboot requested" not in args.kernel_file.read_bytes()):
        parser.error("automatic recovery requires --safe-reboot and the automatic recovery kernel patch")
    if args.tas_init_test and b"mi8speaker: applying unpowered init transcript" not in args.kernel_file.read_bytes():
        parser.error("--tas-init-test requires the isolated kernel with device-private TAS data")
    out = args.output.resolve()
    runs = (ROOT / "work/runs").resolve()
    if out == runs or not out.is_relative_to(runs) or out.exists():
        raise ValueError("Output must be a new directory under work/runs")
    if not os.environ.get("THEOS"):
        raise ValueError("Set THEOS to the verified toolchain and SDK")
    check_layout(args.guid_file)
    apply_loader_patch()
    install_save_config()
    build_env = os.environ.copy()
    if args.preferences_debug or args.preferences_icon_fix:
        build_env["INJECT"] = "\n".join([
            "/redwood/lib/libredwood.dylib backboardd containermanagerd containermanagerd_system",
            "/redwood/lib/libredwood-sb.dylib SpringBoard",
            "/redwood/lib/libredwood-media.dylib mediaserverd",
            "/redwood/lib/libredwood-app.dylib Sileo Preferences",
            "/redwood/lib/libredwood-tweaks.dylib Preferences",
        ])
    app_source = KERNEL / "userland/src/inject_app.c"
    original_app_source = app_source.read_bytes() if args.preferences_icon_fix else None
    saver_source = KERNEL / "userland/src/var_store.c"
    loader_source = KERNEL / "xnuloader/loader.c"
    original_saver = saver_source.read_bytes() if args.safe_reboot else None
    original_loader = loader_source.read_bytes() if args.safe_reboot else None
    try:
        if args.safe_reboot:
            for name in ("safe-reboot-userland.patch", "safe-reboot-loader.patch"):
                patch = ROOT / "patches" / name
                run("patch", "-p1", "--dry-run", "-i", str(patch), cwd=KERNEL)
                run("patch", "-p1", "-i", str(patch), cwd=KERNEL)
            if args.audio_dma_test or args.auto_recovery_test or args.tas_init_test:
                patch = ROOT / "patches/audio-auto-recovery-loader.patch"
                run("patch", "-p1", "--dry-run", "-i", str(patch), cwd=KERNEL)
                run("patch", "-p1", "-i", str(patch), cwd=KERNEL)
        if original_app_source is not None:
            workaround = (ROOT / "system/userland/preferences-icon-fix.c").read_bytes()
            app_source.write_bytes(original_app_source + b"\n" + workaround + b"\n")
        run("bash", "tools/build/build-userland.sh", env=build_env)
        run("bash", "tools/build/build-loader-uefi.sh")
    finally:
        if original_app_source is not None:
            app_source.write_bytes(original_app_source)
        if original_saver is not None:
            saver_source.write_bytes(original_saver)
        if original_loader is not None:
            loader_source.write_bytes(original_loader)
    payload_command = ["python3", "tools/build/build-payload.py", "--output", str(out), "--seconds", "300"]
    if args.wifi:
        payload_command.append("--wifi")
    if args.kernel_file is not None:
        payload_command.extend(["--kernel-file", str(args.kernel_file.resolve())])
    run(*payload_command)
    cmd = (out / "cmdline.txt").read_text().strip()
    cmd, count = re.subn(r"\bloader_return_after=300\s+", "", cmd, count=1)
    if count != 1:
        raise ValueError("Unexpected bounded payload command line")
    cmd += " ufs_write=1 var_store=1 var_save_ms=120000 battery=1"
    if args.audio_probe:
        cmd += " mi8rp_step=5"
    if args.audio_start_test:
        cmd += " mi8rp_step=8"
    if args.audio_glink_test:
        cmd += " mi8rp_step=9"
    if args.audio_apr_test:
        cmd += " mi8rp_step=10"
    if args.audio_afe_test:
        cmd += " mi8rp_step=10 mi8_afe_probe=1"
    if args.audio_dma_test:
        cmd += " mi8rp_step=10 mi8_dma_probe=1 auto_reboot_after=240"
    if args.auto_recovery_test:
        cmd += " auto_reboot_after=180"
    if args.tas_init_test:
        cmd += " mi8_tas_probe=1 auto_reboot_after=240"
    prefix = ("debug=0x8 rd=md0 vtimer_intid=19 hv_wdt_pet=1 ufs_shared_pa=0x" + "0" * 16
              + " battery_shared_pa=0x" + "0" * 16 + " ")
    if len((prefix + cmd).encode()) >= 608:
        raise ValueError("Boot arguments exceed the conservative loader allowance")
    (out / "cmdline-persistent.txt").write_text(cmd + "\n")
    (out / "autousb.bin").write_bytes(b"1\0")
    payload = out / "autoboot.xnup"
    run("python3", str(KERNEL / "xnuloader/mkpayload.py"), "-o", str(payload),
        "kernel=" + str(out / "kernel.macho"), "ramdisk=" + str(out / "root.apfs"),
        "flashboot=" + str(out / "flashboot.apfs"), "autousb=" + str(out / "autousb.bin"),
        "--cmdline", cmd)
    data = payload.read_bytes()
    if len(data) >= 0x03F00000 or struct.unpack_from("<II", data) != (0x50554E58, len(data) - 8):
        raise ValueError("Invalid or oversized XNUP payload")
    env = os.environ.copy()
    env.update(XNU_PAYLOAD=str(payload), MI8_ROOT=str(ROOT),
               EDK2_ROOT=str(ROOT / "work/edk2/Common/edk2"),
               PATH=str(ROOT / "work/host-bin") + os.pathsep + env["PATH"])
    run("bash", "build.sh", "-d", "dipper", cwd=ROOT / "work/edk2", env=env)
    shutil.copy2(ROOT / "work/edk2/boot-dipper.img", out / "boot-persistent.img")
    verify_image(out, data)
    (out / "manifest.json").rename(out / "baseline-manifest.json")
    (out / "autoboot-manifest.json").write_text(json.dumps({
        "scope": "host-only, device-specific experimental RAM boot",
        "storage_writes": True,
        "persistent_user_state": True,
        "periodic_save_seconds": 600,
        "automatic_return_seconds": None,
        "battery_enabled": True,
        "wifi_enabled": args.wifi,
        "audio_probe": args.audio_probe,
        "audio_start_test": args.audio_start_test,
        "audio_glink_test": args.audio_glink_test,
        "audio_apr_test": args.audio_apr_test,
        "audio_afe_test": args.audio_afe_test,
        "audio_dma_test": args.audio_dma_test,
        "auto_recovery_test": args.auto_recovery_test,
        "tas_init_test": args.tas_init_test,
        "safe_reboot": args.safe_reboot,
        "preferences_debug": args.preferences_debug or args.preferences_icon_fix,
        "preferences_icon_fix": args.preferences_icon_fix,
        "cmdline": cmd,
        "image_sha256": hashlib.sha256((out / "boot-persistent.img").read_bytes()).hexdigest(),
    }, indent=2) + "\n")
    print("Host build only. Verify the phone and storage before a temporary boot.")


if __name__ == "__main__":
    main()
