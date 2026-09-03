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
#   screen-focus-system.ppm System after it is clicked and focused
#   screen-focus-demo.ppm   Demo after it is clicked, focused, and raised
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
SHOT_FOCUS_SYSTEM="${SHOT_FOCUS_SYSTEM:-$BUILD_DIR/screen-focus-system.ppm}"
SHOT_FOCUS_DEMO="${SHOT_FOCUS_DEMO:-$BUILD_DIR/screen-focus-demo.ppm}"
SHOT_DRAG_RELEASE="${SHOT_DRAG_RELEASE:-$BUILD_DIR/screen-drag-release.ppm}"
# M17 and M18. Demo has the workspace to itself for everything above, which is
# what one window looks like under a tiling layout. These bring the other three
# in and take them out again, so the two, three and four window layouts are
# demonstrated rather than described.
SHOT_TILES_TWO="${SHOT_TILES_TWO:-$BUILD_DIR/screen-tiles-two.ppm}"
SHOT_TILES_THREE="${SHOT_TILES_THREE:-$BUILD_DIR/screen-tiles-three.ppm}"
SHOT_TILES_FOUR="${SHOT_TILES_FOUR:-$BUILD_DIR/screen-tiles-four.ppm}"
# M9. Three presses down and eight left, so the checker can look for exactly
# three and exactly eight steps rather than just "it moved".
STEER_DOWN_KEYS="${STEER_DOWN_KEYS:-down down down}"
STEER_LEFT_KEYS="${STEER_LEFT_KEYS:-left left left left left left left left}"
# M10. The M9 sequence leaves the rectangle eight pixels from the corridor's
# lower edge. One more down wraps vertically; twenty-three left presses cross
# x=0 in Demo's narrower corridor and land under M11's known press point.
WRAP_LEFT_PRESSES="${WRAP_LEFT_PRESSES:-23}"
ASSIGN_KEYS="${ASSIGN_KEYS:-x equal 5 ret}"
VAR_KEYS="${VAR_KEYS:-x kp_add 3 ret}"
VARIF_KEYS="${VARIF_KEYS:-i f spc x shift-dot 2 spc t h e n spc 1 0 spc e l s e spc 2 0 ret}"
# Typing pace. The kernel polls the keyboard far faster than this.
TYPE_DELAY="${TYPE_DELAY:-0.25}"
# How far to push the mouse. tests/check_boot.py checks the cursor moved by
# exactly this, so the two belong together.
MOUSE_DX="${MOUSE_DX:-120}"
MOUSE_DY="${MOUSE_DY:--60}"
# How far the M11 drag pulls the pointer left, in two equal packets. Named
# because tests/check_boot.py expects exactly this and the aim above has to
# leave room for it.
DRAG_LEFT=180
# Which key to inject. tests/check_boot.py expects this letter on screen.
KEY="${KEY:-a}"
DEBUG_LOG="$BUILD_DIR/debug.log"
# The second boot writes its own log. Proving the disk survives a restart needs
# two runs, and one log with two boots in it cannot say which line came from
# which.
DEBUG_LOG_AGAIN="$BUILD_DIR/debug-again.log"
# Its own disk, not the one `make run` keeps. This one is blanked every run,
# and blanking the disk somebody has been using would be a poor way to find
# that out.
DISK="$BUILD_DIR/me-os-test-disk.img"
SHOT_RESTART="${SHOT_RESTART:-$BUILD_DIR/screen-restart.ppm}"
SHOT_SCROLLBACK="${SHOT_SCROLLBACK:-$BUILD_DIR/screen-scrollback.ppm}"
SERIAL_LOG="$BUILD_DIR/serial.log"
QEMU="${QEMU:-qemu-system-x86_64}"
# Seconds to let OVMF and Limine finish before grabbing the screen.
BOOT_WAIT="${BOOT_WAIT:-16}"

fail() { echo "boot-capture: $*" >&2; exit 1; }

[ -f "$ISO" ] || fail "missing $ISO, run make first"

# A blank disk for every run. The whole point of the second boot is that this
# run put something on it, and a disk left over from last time would pass that
# check without the kernel having written a byte.
truncate -s 0 "$DISK" 2>/dev/null || fail "could not make a disk at $DISK"
truncate -s 4M "$DISK" || fail "could not size the disk at $DISK"
command -v "$QEMU" >/dev/null 2>&1 || fail "missing $QEMU, run make check-tools"

: "${OVMF_CODE:?set OVMF_CODE, or run this through make test}"
: "${OVMF_VARS_LOCAL:?set OVMF_VARS_LOCAL, or run this through make test}"
[ -f "$OVMF_CODE" ] || fail "OVMF firmware not found at $OVMF_CODE"
[ -f "$OVMF_VARS_LOCAL" ] || fail "OVMF variable store not found at $OVMF_VARS_LOCAL"

# Truncate rather than delete, so a failed run cannot leave a stale
# screenshot behind for the checker to pass on.
: > "$SHOT_TILES_TWO"
: > "$SHOT_TILES_THREE"
: > "$SHOT_TILES_FOUR"
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
: > "$SHOT_FOCUS_SYSTEM"
: > "$SHOT_FOCUS_DEMO"
: > "$SHOT_DRAG_RELEASE"
: > "$DEBUG_LOG"
: > "$SERIAL_LOG"


# Walks the pointer to an absolute screen position.
#
# The mouse is relative, so this pins it at the top left first with packets
# small enough that the PS/2 overflow bits never set, then steps to the target.
# Written as a function because M11 needs to land inside a rectangle that is
# moving, and the place it has to land is worked out at the time rather than
# written down here.
move_pointer_to() {
    local tx=$1 ty=$2 sx sy i
    for ((i = 0; i < 20; i++)); do
        echo "mouse_move -100 -100"
        sleep 0.03
    done
    while [ "$tx" -gt 0 ] || [ "$ty" -gt 0 ]; do
        sx=$(( tx > 100 ? 100 : tx ))
        sy=$(( ty > 100 ? 100 : ty ))
        echo "mouse_move $sx $sy"
        sleep 0.03
        tx=$(( tx - sx ))
        ty=$(( ty - sy ))
    done
}

# Types one line at the terminal and presses Enter.
#
# The monitor names keys rather than taking characters, and the few punctuation
# marks a path needs have names of their own. Anything not listed is sent as
# itself, which covers the letters and the digits.
type_line() {
    local text="$1" i c
    for ((i = 0; i < ${#text}; i++)); do
        c="${text:$i:1}"
        case "$c" in
            " ") echo "sendkey spc" ;;
            "/") echo "sendkey slash" ;;
            ".") echo "sendkey dot" ;;
            ">") echo "sendkey shift-dot" ;;
            "|") echo "sendkey shift-backslash" ;;
            "-") echo "sendkey minus" ;;
            *)   echo "sendkey $c" ;;
        esac
        sleep 0.07
    done
    echo "sendkey ret"
    sleep 0.6
}

# The middle of the M5 rectangle, in screen coordinates, from what the kernel
# has most recently said about itself. The rectangle drifts, so its place is a
# function of how long the machine has been up and cannot be worked out here.
#
# It also waits for the rectangle to be far enough from the left edge that the
# drag below has room. A drag that runs into the edge is clamped, and the check
# would then read a preserved offset as a broken one when nothing is broken.
rectangle_centre() {
    local tile rect cx cy rx ry rw rh aim_x aim_y
    tile=$(grep "me-os: tile DEMO at " "$SERIAL_LOG" | tail -1)
    rect=$(grep "me-os: rectangle at " "$SERIAL_LOG" | tail -1)
    if [ -z "$tile" ] || [ -z "$rect" ]; then
        # Nothing to aim at. The middle of the screen at least presses
        # something, and the check that follows says clearly what went wrong.
        echo "640 500"
        return
    fi
    cx=$(echo "$tile" | sed 's/.* client \([0-9]*\),.*/\1/')
    cy=$(echo "$tile" | sed 's/.* client [0-9]*,\([0-9]*\) .*/\1/')
    rx=$(echo "$rect" | sed 's/.* rectangle at \([0-9]*\),.*/\1/')
    ry=$(echo "$rect" | sed 's/.* rectangle at [0-9]*,\([0-9]*\) .*/\1/')
    rw=$(echo "$rect" | sed 's/.*size \([0-9]*\)x.*/\1/')
    rh=$(echo "$rect" | sed 's/.*size [0-9]*x\([0-9]*\).*/\1/')

    # A quarter of the way in rather than the middle. The rectangle drifts while
    # the pointer is being moved, so relative to it the cursor slides the other
    # way, and starting nearer the leading edge leaves longer before the press
    # would fall off it.
    aim_x=$(( cx + rx + rw / 4 ))
    aim_y=$(( cy + ry + rh / 2 ))

    # Which way to drag, decided from where the cursor will actually be rather
    # than from where the rectangle is.
    #
    # This used to wait for the rectangle to drift somewhere with room to its
    # left, which is not something it reliably does: it stops moving while a
    # window is being laid out, and the wait would time out and then aim
    # wherever it had got to. A drag that ran into the edge of the screen was
    # reported as a wrong offset, which is not what had gone wrong.
    #
    # There is no waiting now. Whichever side has room is the side it drags to,
    # and the distance is written down for tests/check_boot.py to read, so the
    # test asserts what was actually asked for.
    if [ "$aim_x" -ge $(( DRAG_LEFT + 80 )) ]; then
        DRAG_DX=$(( -DRAG_LEFT ))
    else
        DRAG_DX=$DRAG_LEFT
    fi
    echo "$DRAG_DX" > "$BUILD_DIR/drag-delta.txt"
    echo "boot-capture: aiming the drag at $aim_x,$aim_y and pulling $DRAG_DX" >&2
    echo "$aim_x $aim_y"
}

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
    # M11. Land the pointer in the middle of the rectangle, wherever the M5
    # drift has carried it to. The kernel reports its position once a second, so
    # aiming is a matter of reading that rather than guessing at a place it will
    # happen to be.
    read -r TARGET_X TARGET_Y <<< "$(rectangle_centre)"
    # Out of the file rather than out of a variable. `rectangle_centre` runs in
    # a command substitution, which is its own shell, so what it set in there
    # did not come back out.
    DRAG_DX=$(cat "$BUILD_DIR/drag-delta.txt")
    move_pointer_to "$TARGET_X" "$TARGET_Y"
    sleep 1
    echo "screendump $SHOT_DRAG_READY"
    sleep 1
    echo "mouse_button 1"
    sleep "$TYPE_DELAY"
    echo "screendump $SHOT_FOCUS_DEMO"
    sleep 2
    echo "mouse_move $(( DRAG_DX / 2 )) 15"
    sleep "$TYPE_DELAY"
    echo "mouse_move $(( DRAG_DX / 2 )) 15"
    sleep 2
    echo "screendump $SHOT_DRAG_HELD"
    sleep 2
    echo "mouse_button 0"
    sleep "$TYPE_DELAY"
    echo "mouse_move 80 0"
    sleep 2
    echo "screendump $SHOT_DRAG_RELEASE"
    sleep 2

    # M17 and M18. Ctrl S brings every hidden window back, so four windows tile
    # into a two by two grid. Ctrl H takes the focused one out again, and the
    # rest grow into the space it leaves. Nothing here is dragged or positioned:
    # the layout decides, which is the whole point.
    echo "sendkey ctrl-s"
    sleep 2
    echo "screendump $SHOT_TILES_FOUR"
    sleep 1
    echo "sendkey ctrl-h"
    sleep 2
    echo "screendump $SHOT_TILES_THREE"
    sleep 1
    echo "sendkey ctrl-h"
    sleep 2
    echo "screendump $SHOT_TILES_TWO"
    sleep 1
    # Focus moves between tiles from the keyboard, and back to a four window
    # layout so the last capture and the last logged layout agree.
    echo "sendkey ctrl-right"
    sleep 1
    echo "sendkey ctrl-left"
    sleep 1
    echo "sendkey ctrl-s"
    sleep 2

    # M19. Focus the terminal by clicking its taskbar button, which is where a
    # person would click and is a fixed place, unlike counting focus steps that
    # depend on what is showing. Then type at it: VER and CPU both answer with
    # something the kernel had to look up rather than something written down.
    #
    # The button sits after the launcher and three other windows, at the middle
    # of the fourth. Those numbers come from kernel/src/shell.c.
    move_pointer_to 589 783
    sleep 1
    echo "mouse_button 1"
    sleep "$TYPE_DELAY"
    echo "mouse_button 0"
    sleep 1
    type_line "ver"
    type_line "cpu"

    # M20. The filesystem, driven the way a person would: look around, go
    # somewhere, read a file the kernel wrote, then make one and read it back.
    type_line "pwd"
    type_line "ls /"
    type_line "cd /docs"
    type_line "cat readme.txt"
    type_line "cd /home"
    type_line "mkdir projects"
    type_line "echo tiling works > projects/note.txt"
    type_line "cat projects/note.txt"
    type_line "ls projects"

    # M21. Open a file in the editor, type three lines into it, save it with
    # Ctrl O, then go back to the terminal and read it out. That is the whole
    # loop a person would use, and every part of it is real.
    type_line "edit todo.txt"
    sleep 1
    type_line "me os todo"
    type_line "  workspaces next"
    type_line "  then a disk driver"
    echo "sendkey ctrl-o"
    sleep 1
    echo "sendkey ctrl-left"
    sleep 1
    type_line "cat todo.txt"
    type_line "wc todo.txt"
    # M23. Written at the root, so the second boot finds it in the one listing
    # the kernel prints when it loads a disk.
    type_line "write /persist.txt the disk kept this"
    # M25. A pipe and an arrow, both on commands that never had either. LS is
    # not ECHO, and the file it writes has to hold what LS would have shown.
    type_line "ls | sort > sorted.txt"
    type_line "cat sorted.txt | grep txt"
    type_line "head 2 sorted.txt"
    type_line "df"
    # M26. Fill the screen, then look back at what scrolled off it. HELP is the
    # longest thing the shell prints, so it is what pushes lines off the top.
    # M27. Write a script from the shell, then run it. Nothing about it is
    # special cased: RUN reads the same lines a person would type.
    type_line "write setup.txt mkdir /made-by-script"
    type_line "run setup.txt"
    type_line "ls"
    type_line "help"
    echo "sendkey pgup"
    sleep 1
    echo "sendkey pgup"
    sleep 1
    echo "screendump $SHOT_SCROLLBACK"
    sleep 1
    echo "sendkey pgdn"
    sleep 1
    echo "sendkey pgdn"
    sleep 1
    type_line "date"
    sleep 2

    # M22. Send two windows to the next workspace and go and look at them. The
    # last capture is taken there, so the final layout in the log and the final
    # screenshot are the same set of windows.
    echo "sendkey ctrl-m"
    sleep 1
    echo "sendkey ctrl-m"
    sleep 1
    echo "sendkey ctrl-2"
    sleep 2
    echo "screendump $SHOT_FOCUS_SYSTEM"
    sleep 2
    sleep 2
    echo "quit"
} | "$QEMU" \
        -machine q35 -m 512M -cdrom "$ISO" -boot d \
        -drive if=pflash,unit=0,format=raw,readonly=on,file="$OVMF_CODE" \
        -drive if=pflash,unit=1,format=raw,file="$OVMF_VARS_LOCAL" \
        -drive file="$DISK",format=raw,if=none,id=medisk \
        -device isa-ide,id=meide -device ide-hd,drive=medisk,bus=meide.0 \
        -display none -monitor stdio \
        -debugcon "file:$DEBUG_LOG" \
        -serial "file:$SERIAL_LOG" \
        -no-reboot -no-shutdown > /dev/null

# M23. The same disk, a second time, with the CD still the only boot device.
#
# This is the whole milestone in one step. The kernel that comes up here was
# given nothing but the ISO and a disk the last run wrote, so anything it knows
# about /PERSIST.TXT it read off that disk. Nothing is typed: if the file has to
# be asked for it has already been proved.
echo "boot-capture: restarting with the same disk to see what survived"
{
    sleep "$BOOT_WAIT"
    sleep 4
    echo "screendump $SHOT_RESTART"
    sleep 2
    echo "quit"
} | "$QEMU" \
        -machine q35 -m 512M -cdrom "$ISO" -boot d \
        -drive if=pflash,unit=0,format=raw,readonly=on,file="$OVMF_CODE" \
        -drive if=pflash,unit=1,format=raw,file="$OVMF_VARS_LOCAL" \
        -drive file="$DISK",format=raw,if=none,id=medisk \
        -device isa-ide,id=meide -device ide-hd,drive=medisk,bus=meide.0 \
        -display none -monitor stdio \
        -debugcon "file:$DEBUG_LOG_AGAIN" \
        -no-reboot -no-shutdown > /dev/null

[ -s "$SHOT_RESTART" ] || fail "QEMU produced no screenshot at $SHOT_RESTART"
[ -s "$SHOT_SCROLLBACK" ] || fail "QEMU produced no screenshot at $SHOT_SCROLLBACK"

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
[ -s "$SHOT_FOCUS_SYSTEM" ] || fail "QEMU produced no screenshot at $SHOT_FOCUS_SYSTEM"
[ -s "$SHOT_FOCUS_DEMO" ] || fail "QEMU produced no screenshot at $SHOT_FOCUS_DEMO"
[ -s "$SHOT_DRAG_RELEASE" ] || fail "QEMU produced no screenshot at $SHOT_DRAG_RELEASE"
[ -s "$SHOT_TILES_FOUR" ] || fail "QEMU produced no screenshot at $SHOT_TILES_FOUR"
[ -s "$SHOT_TILES_THREE" ] || fail "QEMU produced no screenshot at $SHOT_TILES_THREE"
[ -s "$SHOT_TILES_TWO" ] || fail "QEMU produced no screenshot at $SHOT_TILES_TWO"

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
echo "boot-capture: dragged the rectangle $(cat "$BUILD_DIR/drag-delta.txt") 30, released it, then moved the pointer 80 0"
echo "boot-capture: showed all four windows, hid two, moved focus, and showed them again"
if [ -s "$DEBUG_LOG_AGAIN" ]; then
    echo "boot-capture: kernel log of the second boot"
    sed 's/^/    /' "$DEBUG_LOG_AGAIN"
fi
