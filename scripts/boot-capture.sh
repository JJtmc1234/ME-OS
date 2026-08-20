#!/usr/bin/env bash
# Boots the ME OS ISO headless, captures the framebuffer, then quits QEMU.
#
# Writes:
#   build/screen-boot.ppm  the framebuffer just after boot
#   build/screen-key.ppm   the framebuffer after one key press is injected
#   build/debug.log        kernel log via QEMU's debug port
#   build/serial.log       the same log via COM1, which real hardware also has
#
# The key press is sent through the QEMU monitor, so M2 can be checked
# without anyone sitting at a keyboard.
#
# Nothing here touches a host disk or device.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ISO="build/me-os.iso"
SHOT_BOOT="${SHOT_BOOT:-build/screen-boot.ppm}"
SHOT_KEY="${SHOT_KEY:-build/screen-key.ppm}"
# Which key to inject. tests/check_boot.py expects this letter on screen.
KEY="${KEY:-a}"
DEBUG_LOG="build/debug.log"
SERIAL_LOG="build/serial.log"
QEMU="${QEMU:-qemu-system-x86_64}"
# Seconds to let OVMF and Limine finish before grabbing the screen.
BOOT_WAIT="${BOOT_WAIT:-12}"

fail() { echo "boot-capture: $*" >&2; exit 1; }

[ -f "$ISO" ] || fail "missing $ISO, run make first"
command -v "$QEMU" >/dev/null 2>&1 || fail "missing $QEMU, run make check-tools"

: "${OVMF_CODE:?set OVMF_CODE, or run this through make test}"
: "${OVMF_VARS_LOCAL:?set OVMF_VARS_LOCAL, or run this through make test}"
[ -f "$OVMF_CODE" ] || fail "OVMF firmware not found at $OVMF_CODE"
[ -f "$OVMF_VARS_LOCAL" ] || fail "OVMF variable store not found at $OVMF_VARS_LOCAL"

# Truncate rather than delete, so a failed run cannot leave a stale
# screenshot behind for the checker to pass on.
: > "$SHOT_BOOT"
: > "$SHOT_KEY"
: > "$DEBUG_LOG"
: > "$SERIAL_LOG"

# The monitor reads these commands from stdin once the guest has booted.
{
    sleep "$BOOT_WAIT"
    echo "screendump $SHOT_BOOT"
    sleep 2
    echo "sendkey $KEY"
    sleep 2
    echo "screendump $SHOT_KEY"
    sleep 2
    echo "quit"
} | "$QEMU" \
        -machine q35 -m 512M -cdrom "$ISO" -boot d \
        -drive if=pflash,unit=0,format=raw,readonly=on,file="$OVMF_CODE" \
        -drive if=pflash,unit=1,format=raw,file="$OVMF_VARS_LOCAL" \
        -display none -monitor stdio \
        -debugcon "file:$DEBUG_LOG" \
        -serial "file:$SERIAL_LOG" \
        -no-reboot -no-shutdown > /dev/null

[ -s "$SHOT_BOOT" ] || fail "QEMU produced no screenshot at $SHOT_BOOT"
[ -s "$SHOT_KEY" ] || fail "QEMU produced no screenshot at $SHOT_KEY"

echo "boot-capture: screens $SHOT_BOOT and $SHOT_KEY (key sent: $KEY)"
if [ -s "$DEBUG_LOG" ]; then
    echo "boot-capture: kernel log"
    sed 's/^/    /' "$DEBUG_LOG"
fi
