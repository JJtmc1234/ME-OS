#!/usr/bin/env bash
# Boots the ME OS ISO headless, captures the framebuffer, then exits QEMU.
# Used to verify M1 without needing a graphical display.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ISO="build/me-os.iso"
SHOT="${SHOT:-build/screen.ppm}"
LOG="build/debug.log"
# Seconds to let OVMF and Limine finish before grabbing the screen.
BOOT_WAIT="${BOOT_WAIT:-12}"

if [ ! -f "$ISO" ]; then
    echo "missing $ISO, run make first" >&2
    exit 1
fi

# The monitor reads these commands from stdin once the guest has booted.
{
    sleep "$BOOT_WAIT"
    echo "screendump $SHOT"
    sleep 2
    echo "quit"
} | qemu-system-x86_64 \
        -machine q35 -m 512M -cdrom "$ISO" -boot d \
        -drive if=pflash,unit=0,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
        -drive if=pflash,unit=1,format=raw,file=build/OVMF_VARS.fd \
        -display none -monitor stdio \
        -debugcon "file:$LOG" \
        -no-reboot -no-shutdown > /dev/null

echo "screenshot: $SHOT"
if [ -s "$LOG" ]; then
    echo "kernel debug output:"
    cat "$LOG"
fi
