#!/usr/bin/env bash
# Boots the ME OS ISO headless, captures the framebuffer, then quits QEMU.
#
# Writes to BUILD_DIR, which defaults to build/:
#   screen-boot.ppm   the framebuffer just after boot
#   screen-key.ppm    the framebuffer after one key press is injected
#   screen-mouse.ppm  the framebuffer after the mouse is moved
#   screen-clamp.ppm  the framebuffer after the mouse is shoved hard
#                           toward the corner, to show repeated movement works
#                           and the cursor stays on screen
#   screen-sum.ppm    the framebuffer after a sum is typed and evaluated
#   screen-power.ppm  the framebuffer after a power is typed, which needs
#                           a shifted key, and evaluated
#   screen-true.ppm   a conditional whose condition holds
#   screen-false.ppm  the same conditional with the condition reversed
#   screen-assign.ppm a value stored under a name
#   screen-var.ppm    that name used in a sum on a later line
#   screen-varif.ppm  that name used in a conditional on a later line
#   screen-wrap-down.ppm the steered rectangle after crossing its lower edge
#   screen-wrap-left.ppm the steered rectangle after crossing its left edge
#   screen-drag-ready.ppm pointer placed inside the rectangle before pressing
#   screen-drag-held.ppm  pointer and rectangle moved while left is held
#   screen-drag-release.ppm pointer moved again after releasing the rectangle
#   debug.log         kernel log via QEMU's debug port
#   serial.log        the same log via COM1, which real hardware also has
#
# The key press and the mouse movements are sent through the QEMU monitor, so
# M2 and M4 can be checked without anyone at a keyboard or holding a mouse.
#
# Nothing here touches a host disk or device.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
ISO="$BUILD_DIR/me-os.iso"
SHOT_BOOT="${SHOT_BOOT:-$BUILD_DIR/screen-boot.ppm}"
SHOT_KEY="${SHOT_KEY:-$BUILD_DIR/screen-key.ppm}"
SHOT_MOUSE="${SHOT_MOUSE:-$BUILD_DIR/screen-mouse.ppm}"
SHOT_CLAMP="${SHOT_CLAMP:-$BUILD_DIR/screen-clamp.ppm}"
SHOT_SUM="${SHOT_SUM:-$BUILD_DIR/screen-sum.ppm}"
# The sum typed on the emulated keyboard. tests/check_boot.py expects this one,
# so the two belong together. It starts with escape because since M8 every
# letter types, so the key injected to prove M2 is sitting on the line.
SUM_KEYS="${SUM_KEYS:-esc 1 2 kp_add 3 0 ret}"
SHOT_POWER="${SHOT_POWER:-$BUILD_DIR/screen-power.ppm}"
# shift-6 is a caret, which is also a check that shift decoding works.
POWER_KEYS="${POWER_KEYS:-2 shift-6 5 ret}"
SHOT_TRUE="${SHOT_TRUE:-$BUILD_DIR/screen-true.ppm}"
SHOT_FALSE="${SHOT_FALSE:-$BUILD_DIR/screen-false.ppm}"
# IF 3>2 THEN 10 ELSE 20, and the same with the comparison reversed. shift-dot
# is the greater than sign.
TRUE_KEYS="${TRUE_KEYS:-i f spc 3 shift-dot 2 spc t h e n spc 1 0 spc e l s e spc 2 0 ret}"
FALSE_KEYS="${FALSE_KEYS:-i f spc 2 shift-dot 3 spc t h e n spc 1 0 spc e l s e spc 2 0 ret}"
# M8. Store 5 under X, then use X on two later lines. The unshifted equals key
# is the assignment, which is why it is not the key that evaluates.
SHOT_ASSIGN="${SHOT_ASSIGN:-$BUILD_DIR/screen-assign.ppm}"
SHOT_VAR="${SHOT_VAR:-$BUILD_DIR/screen-var.ppm}"
SHOT_VARIF="${SHOT_VARIF:-$BUILD_DIR/screen-varif.ppm}"
SHOT_STEER_DOWN="${SHOT_STEER_DOWN:-$BUILD_DIR/screen-steer-down.ppm}"
SHOT_STEER_LEFT="${SHOT_STEER_LEFT:-$BUILD_DIR/screen-steer-left.ppm}"
SHOT_WRAP_DOWN="${SHOT_WRAP_DOWN:-$BUILD_DIR/screen-wrap-down.ppm}"
SHOT_WRAP_LEFT="${SHOT_WRAP_LEFT:-$BUILD_DIR/screen-wrap-left.ppm}"
SHOT_DRAG_READY="${SHOT_DRAG_READY:-$BUILD_DIR/screen-drag-ready.ppm}"
SHOT_DRAG_HELD="${SHOT_DRAG_HELD:-$BUILD_DIR/screen-drag-held.ppm}"
SHOT_DRAG_RELEASE="${SHOT_DRAG_RELEASE:-$BUILD_DIR/screen-drag-release.ppm}"
# M9. Three presses down and eight left, so the checker can look for exactly
# three and exactly eight steps rather than just "it moved".
STEER_DOWN_KEYS="${STEER_DOWN_KEYS:-down down down}"
STEER_LEFT_KEYS="${STEER_LEFT_KEYS:-left left left left left left left left}"
# M10. The M9 sequence leaves the rectangle eight pixels from the corridor's
# lower edge. One more down wraps vertically; sixty-one left presses travel
# farther than the widest possible horizontal range and therefore cross x=0.
WRAP_LEFT_PRESSES="${WRAP_LEFT_PRESSES:-61}"
ASSIGN_KEYS="${ASSIGN_KEYS:-x equal 5 ret}"
VAR_KEYS="${VAR_KEYS:-x kp_add 3 ret}"
VARIF_KEYS="${VARIF_KEYS:-i f spc x shift-dot 2 spc t h e n spc 1 0 spc e l s e spc 2 0 ret}"
# Typing pace. The kernel polls the keyboard far faster than this.
TYPE_DELAY="${TYPE_DELAY:-0.25}"
# How far to push the mouse. tests/check_boot.py checks the cursor moved by
# exactly this, so the two belong together.
MOUSE_DX="${MOUSE_DX:-120}"
MOUSE_DY="${MOUSE_DY:--60}"
# Which key to inject. tests/check_boot.py expects this letter on screen.
KEY="${KEY:-a}"
DEBUG_LOG="$BUILD_DIR/debug.log"
SERIAL_LOG="$BUILD_DIR/serial.log"
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
: > "$SHOT_MOUSE"
: > "$SHOT_CLAMP"
: > "$SHOT_SUM"
: > "$SHOT_POWER"
: > "$SHOT_TRUE"
: > "$SHOT_FALSE"
: > "$SHOT_ASSIGN"
: > "$SHOT_VAR"
: > "$SHOT_VARIF"
: > "$SHOT_STEER_DOWN"
: > "$SHOT_STEER_LEFT"
: > "$SHOT_WRAP_DOWN"
: > "$SHOT_WRAP_LEFT"
: > "$SHOT_DRAG_READY"
: > "$SHOT_DRAG_HELD"
: > "$SHOT_DRAG_RELEASE"
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
    echo "mouse_move $MOUSE_DX $MOUSE_DY"
    sleep 2
    echo "screendump $SHOT_MOUSE"
    sleep 2
    # Four hard shoves toward the bottom right corner. The emulator caps how
    # far one packet can move the pointer, so this travels a long way rather
    # than pinning it to the edge.
    echo "mouse_move 400 400"
    echo "mouse_move 400 400"
    echo "mouse_move 400 400"
    echo "mouse_move 400 400"
    sleep 2
    echo "screendump $SHOT_CLAMP"
    sleep 2
    # Type a sum one key at a time, then press enter to evaluate it.
    for key in $SUM_KEYS; do
        echo "sendkey $key"
        sleep "$TYPE_DELAY"
    done
    sleep 2
    echo "screendump $SHOT_SUM"
    sleep 2
    for key in $POWER_KEYS; do
        echo "sendkey $key"
        sleep "$TYPE_DELAY"
    done
    sleep 2
    echo "screendump $SHOT_POWER"
    sleep 2
    for key in $TRUE_KEYS; do
        echo "sendkey $key"
        sleep "$TYPE_DELAY"
    done
    sleep 2
    echo "screendump $SHOT_TRUE"
    sleep 2
    for key in $FALSE_KEYS; do
        echo "sendkey $key"
        sleep "$TYPE_DELAY"
    done
    sleep 2
    echo "screendump $SHOT_FALSE"
    sleep 2
    # M8. Each of these is a separate line, so the last two only work if the
    # value stored by the first one is still there.
    for key in $ASSIGN_KEYS; do
        echo "sendkey $key"
        sleep "$TYPE_DELAY"
    done
    sleep 2
    echo "screendump $SHOT_ASSIGN"
    sleep 2
    for key in $VAR_KEYS; do
        echo "sendkey $key"
        sleep "$TYPE_DELAY"
    done
    sleep 2
    echo "screendump $SHOT_VAR"
    sleep 2
    for key in $VARIF_KEYS; do
        echo "sendkey $key"
        sleep "$TYPE_DELAY"
    done
    sleep 2
    echo "screendump $SHOT_VARIF"
    sleep 2
    for key in $STEER_DOWN_KEYS; do
        echo "sendkey $key"
        sleep "$TYPE_DELAY"
    done
    sleep 2
    echo "screendump $SHOT_STEER_DOWN"
    sleep 2
    for key in $STEER_LEFT_KEYS; do
        echo "sendkey $key"
        sleep "$TYPE_DELAY"
    done
    sleep 2
    echo "screendump $SHOT_STEER_LEFT"
    sleep 2
    echo "sendkey down"
    sleep "$TYPE_DELAY"
    sleep 2
    echo "screendump $SHOT_WRAP_DOWN"
    sleep 2
    for ((press = 0; press < WRAP_LEFT_PRESSES; press++)); do
        echo "sendkey left"
        sleep "$TYPE_DELAY"
    done
    sleep 2
    echo "screendump $SHOT_WRAP_LEFT"
    sleep 2
    # M11. Pin the pointer at the top-left first, then put it at a known point
    # inside the wrapped rectangle. Small packets avoid the PS/2 overflow bits.
    for ((move = 0; move < 20; move++)); do
        echo "mouse_move -100 -100"
        sleep 0.05
    done
    for ((move = 0; move < 5; move++)); do
        echo "mouse_move 100 100"
        sleep 0.05
    done
    for ((move = 0; move < 3; move++)); do
        echo "mouse_move 100 0"
        sleep 0.05
    done
    echo "mouse_move 0 20"
    sleep 2
    echo "screendump $SHOT_DRAG_READY"
    sleep 2
    echo "mouse_button 1"
    sleep "$TYPE_DELAY"
    echo "mouse_move -90 15"
    sleep "$TYPE_DELAY"
    echo "mouse_move -90 15"
    sleep 2
    echo "screendump $SHOT_DRAG_HELD"
    sleep 2
    echo "mouse_button 0"
    sleep "$TYPE_DELAY"
    echo "mouse_move 80 0"
    sleep 2
    echo "screendump $SHOT_DRAG_RELEASE"
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
[ -s "$SHOT_MOUSE" ] || fail "QEMU produced no screenshot at $SHOT_MOUSE"
[ -s "$SHOT_CLAMP" ] || fail "QEMU produced no screenshot at $SHOT_CLAMP"
[ -s "$SHOT_SUM" ] || fail "QEMU produced no screenshot at $SHOT_SUM"
[ -s "$SHOT_POWER" ] || fail "QEMU produced no screenshot at $SHOT_POWER"
[ -s "$SHOT_TRUE" ] || fail "QEMU produced no screenshot at $SHOT_TRUE"
[ -s "$SHOT_FALSE" ] || fail "QEMU produced no screenshot at $SHOT_FALSE"
[ -s "$SHOT_ASSIGN" ] || fail "QEMU produced no screenshot at $SHOT_ASSIGN"
[ -s "$SHOT_VAR" ] || fail "QEMU produced no screenshot at $SHOT_VAR"
[ -s "$SHOT_VARIF" ] || fail "QEMU produced no screenshot at $SHOT_VARIF"
[ -s "$SHOT_STEER_DOWN" ] || fail "QEMU produced no screenshot at $SHOT_STEER_DOWN"
[ -s "$SHOT_STEER_LEFT" ] || fail "QEMU produced no screenshot at $SHOT_STEER_LEFT"
[ -s "$SHOT_WRAP_DOWN" ] || fail "QEMU produced no screenshot at $SHOT_WRAP_DOWN"
[ -s "$SHOT_WRAP_LEFT" ] || fail "QEMU produced no screenshot at $SHOT_WRAP_LEFT"
[ -s "$SHOT_DRAG_READY" ] || fail "QEMU produced no screenshot at $SHOT_DRAG_READY"
[ -s "$SHOT_DRAG_HELD" ] || fail "QEMU produced no screenshot at $SHOT_DRAG_HELD"
[ -s "$SHOT_DRAG_RELEASE" ] || fail "QEMU produced no screenshot at $SHOT_DRAG_RELEASE"

echo "boot-capture: screens $SHOT_BOOT, $SHOT_KEY, $SHOT_MOUSE, $SHOT_CLAMP, $SHOT_SUM"
echo "boot-capture: key $KEY, mouse moved $MOUSE_DX $MOUSE_DY then shoved at the corner"
echo "boot-capture: typed the sum $SUM_KEYS, then the power $POWER_KEYS"
echo "boot-capture: typed two conditionals, one true and one false"
echo "boot-capture: stored X, then used it in a sum and in a conditional"
if [ -s "$DEBUG_LOG" ]; then
    echo "boot-capture: kernel log"
    sed 's/^/    /' "$DEBUG_LOG"
fi
echo "boot-capture: steered the rectangle with $STEER_DOWN_KEYS then $STEER_LEFT_KEYS"
echo "boot-capture: wrapped it with down then $WRAP_LEFT_PRESSES presses of left"
echo "boot-capture: dragged the rectangle -180 30, released it, then moved the pointer 80 0"
