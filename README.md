# ME OS

ME OS is a from scratch x86-64 operating system. It starts extremely small and
advances through many individually testable milestones.

The long term goal is a general purpose operating system with strong Linux and
Unix compatibility where practical, standard file formats and application
conventions, reliability and recovery as core features, agent native computing,
an agent centric desktop, and eventual integration with Carl and the wider ME
ecosystem.

Status today: a software prototype that boots in QEMU. It has not been booted on
a physical machine. That is a later step, and nothing in this repository writes
to a disk or a USB device.

## Current state

| Milestone | What it proves | Status |
| --- | --- | --- |
| M1 boot proof | UEFI boot through Limine, a framebuffer, and one line of text | Verified software milestone, QEMU |
| M2 keyboard input | A key press reaches the kernel and changes the screen | Verified software milestone, QEMU |
| M3 draw rectangle | A filled rectangle, with M1 and M2 untouched | Verified software milestone, QEMU |
| M4 mouse cursor | A cursor that follows a mouse and stays on screen | Verified software milestone, QEMU |
| M5 move rectangle | The rectangle crosses the screen over time | Verified software milestone, QEMU |
| M6 basic arithmetic | Whole number sums typed on the keyboard, answered on screen | Verified software milestone, QEMU |
| M7 conditionals | One IF, taking either branch, shown on screen | Verified software milestone, QEMU |
| M8 variables | Named values that can be stored and changed | Verified software milestone, QEMU |
| M9 keyboard controlled rectangle | Keys move the rectangle | Next |

All eight milestones are checked automatically. `make test` boots the image
headlessly, injects a key press, moves the mouse, and inspects the resulting
framebuffers. `make test-unit` checks framebuffer clipping, mouse packet
decoding, pointer clamping, arithmetic and the variable table on the
development machine, without an emulator. See
[docs/milestones.md](docs/milestones.md) for the full roadmap.

## Success conditions

**M1.** The system boots over UEFI in a virtual machine and displays, in white
on black, with no crash or reboot afterwards:

```text
IF YOU SEE THIS IT WORKED
```

**M2.** The M1 message stays exactly where it was, a second line below it reads
`PRESS A KEY`, and pressing a supported key replaces that line with
`LAST KEY <key>`. Supported keys are A to Z, 0 to 9, space, enter, escape,
backspace and tab.

**M3.** One filled rectangle, in its own colour, below the key line, with both
lines of text unchanged. It is static: nothing moves it.

**M4.** A cursor is drawn, and moving the mouse moves it. It stays inside the
screen, keeps its shape, and puts back whatever it covered when it moves on.
Nothing follows it and nothing can be dragged.

**M5.** The rectangle crosses the screen at sixty pixels a second, turning
around at each edge. It moves at a rate rather than at whatever speed this
machine runs the loop, because the movement is driven by a clock. Nothing
controls it yet: keys move the rectangle at M9, and it wraps around the edges
instead of turning around at M10.

**M6.** Type a sum and press enter, and the line above the message shows the
answer. Addition, subtraction, multiplication, whole number division and powers,
with the precedence they have on paper. Overflow, division by zero and
fractional powers are refused and shown as `ERROR` rather than producing a wrong
answer or faulting.

**M7.** One conditional, on the same line:

```text
IF 3>2 THEN 10 ELSE 20      shows 10
IF 2>3 THEN 10 ELSE 20      shows 20
IF 5==5 THEN 1 ELSE 0       shows 1
```

The comparison is `=`, `==`, `<` or `>`, and all three places take a sum, so
`IF 2^3=8 THEN 6*7 ELSE 0` gives 42. There is no nesting and there are no
loops: those are later milestones, or no milestone at all. Both branches are
worked out, so a branch that overflows makes the whole line an error even when
it is not the one taken. Enter is the only key that evaluates, because `=` is
something someone might want to type: at M7 as a comparison, and at M8 as an
assignment.

**M8.** A value can be given a name, and the name used on any later line:

```text
X=5                         shows X=5=5
X+3                         shows X+3=8
IF X>2 THEN 10 ELSE 20      shows 10
```

A name is an uppercase letter followed by up to three more letters or digits,
so `X`, `X2` and `SUM1` are all names. Eight names fit, and a ninth is
refused rather than throwing one away. `IF`, `THEN` and `ELSE` are reserved. A name may
be used anywhere a number may be, including inside a condition and inside
either branch. Reading a name that was never given a value is an error rather
than zero, because a typo that quietly reads as zero gives a wrong answer and
says nothing about it. A line that is refused stores nothing, so `X=5 6` leaves
`X` exactly as it was. There is one global table and no scope, no loops, no
functions, no arrays and no strings, and nothing survives a reboot.

Every letter types now, because a variable needs a name. Until M8 a letter was
only accepted where one of `IF`, `THEN` or `ELSE` could still be forming, which
kept a key pressed for some other reason out of the sum. That rule could not
survive variables, so escape clears the line instead.

| Key | In a sum |
| --- | --- |
| 0 to 9 | digits |
| A to Z | a keyword, or the name of a variable |
| = | store what follows under the name in front of it |
| shift and = | plus |
| minus | minus |
| shift and 8, or keypad star | multiply |
| slash | whole number divide |
| shift and 6 | to the power of |
| shift and comma, shift and full stop | less than, greater than |
| enter | work it out |
| backspace | delete the last character |
| escape | clear the line |

## Build and run

```
make            # build build/me-os.iso
make run        # boot it in QEMU with a window, kernel log on the terminal
make test       # boot headless, inject a key, check what was drawn
make test-unit  # framebuffer bounds checks on this machine, no emulator
make check      # check tools, build, then both test suites
make clean      # remove build output
make help       # list every target
```

`make run` is the one to use to see it yourself. Press keys in the QEMU window
and the bottom line changes. Close the window to stop.

## Dependencies

On Debian or Ubuntu:

```
sudo apt install build-essential xorriso qemu-system-x86 ovmf git python3
```

`make check-tools` verifies each one and prints where it found the UEFI
firmware. If OVMF lives somewhere unusual, pass its location:

```
make run OVMF_CODE=/path/OVMF_CODE.fd OVMF_VARS=/path/OVMF_VARS.fd
```

Limine is fetched automatically at a pinned tag on the first build.

## Hardware safety rule

This project never writes to a physical disk, a USB device, or any host device.
Everything it produces is a file under `build/`.

Putting `build/me-os.iso` onto real hardware is a deliberate act a person
performs themselves, with a command they have read and understood, on a device
they have chosen. No script in this repository does it, and none should be added
that does. Writing an image to the wrong device destroys whatever was on it.

## Layout

```
kernel/include/   headers: framebuffer, font, keyboard, mouse, pointer, cursor,
                  timer, rectangle, calculator, variables, logging, memory
kernel/src/       the kernel itself, one small module per concern
boot/             Limine license, and the fetched bootloader (not committed)
scripts/          headless boot capture for testing
tests/            automated checks: what the kernel drew, and framebuffer bounds
docs/             architecture, milestones, emergency alert design notes
linker.ld         higher half kernel layout
limine.conf       bootloader entry
Makefile          the whole build
```

## Reproducible builds

Two clean builds of the same source produce byte identical output, both the
kernel and the ISO. Timestamps, file dates, the GPT identifier and the build
path are all pinned. `SOURCE_DATE_EPOCH`, `ISO_DATE` and `ISO_GUID` can be
overridden if a different fixed value is wanted.

## Documentation

- [docs/architecture.md](docs/architecture.md) how the kernel is put together
- [docs/milestones.md](docs/milestones.md) the milestone roadmap
- [docs/emergency-alerts.md](docs/emergency-alerts.md) design notes for a future
  ME emergency alert system, deliberately not implemented
