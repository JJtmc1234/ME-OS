#!/usr/bin/env bash
# Boots the ISO headless for a few seconds and prints the kernel log.
#
# `make test` is the real check and takes four minutes, because it drives the
# machine with key presses and reads twenty framebuffer captures. While a
# milestone is being built the question is usually much smaller: did the kernel
# reach the end of boot, and what did it say on the way. This answers that in
# about five seconds.
#
# It proves nothing on its own and is not part of `make test`. It is a
# debugging tool, and a milestone is not done because this looked right.
#
# Nothing here writes to a device. The disk is a file under build/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
ISO="${ISO:-$BUILD_DIR/me-os.iso}"
QEMU="${QEMU:-qemu-system-x86_64}"
SECONDS_TO_RUN="${SECONDS_TO_RUN:-5}"
LOG="$BUILD_DIR/quick.log"
DISK="$BUILD_DIR/quick-disk.img"

[ -f "$ISO" ] || { echo "quick-boot: no ISO at $ISO. Run make iso first." >&2; exit 1; }

OVMF_CODE="${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
OVMF_VARS_SRC="${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}"
OVMF_VARS_LOCAL="$BUILD_DIR/quick-OVMF_VARS.fd"
cp -f "$OVMF_VARS_SRC" "$OVMF_VARS_LOCAL"

# A blank disk each time, so nothing here depends on what a previous run left.
rm -f "$DISK"
dd if=/dev/zero of="$DISK" bs=1M count=8 status=none

{
    sleep "$SECONDS_TO_RUN"
    echo "quit"
} | "$QEMU" \
        -machine q35 -m 512M -cdrom "$ISO" -boot d \
        -drive if=pflash,unit=0,format=raw,readonly=on,file="$OVMF_CODE" \
        -drive if=pflash,unit=1,format=raw,file="$OVMF_VARS_LOCAL" \
        -drive file="$DISK",format=raw,if=none,id=medisk \
        -device isa-ide,id=meide -device ide-hd,drive=medisk,bus=meide.0 \
        -display none -monitor stdio \
        -serial "file:$LOG" \
        -no-reboot -no-shutdown > /dev/null 2>&1 || true

echo "--- kernel log ---"
cat "$LOG"
