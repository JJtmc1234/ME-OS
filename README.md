# ME OS

ME OS is a from scratch x86-64 operating system. It starts extremely small and
advances through many individually testable milestones.

The long term goal is a general purpose operating system with strong Linux and
Unix compatibility where practical, standard file formats and application
conventions, reliability and recovery as core features, agent native computing,
an agent centric desktop, and eventual integration with Carl and the wider ME
ecosystem.

Status today: a software prototype that boots from one ISO in both QEMU and
VirtualBox, through either UEFI or BIOS, and reaches a tiling desktop with a
terminal you can type at. It has not been booted on a physical machine. That is
a later step, and nothing in this repository writes to a disk or a USB device.

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
| M12 rotating triangle and floating point | SSE turned on deliberately, and a triangle turning on a timer | Verified software milestone, QEMU |
| M9 keyboard controlled rectangle | The arrow keys steer the rectangle | Verified software milestone, QEMU |
| M10 edge wrapping | Arrow-key movement wraps at all four corridor edges | Verified software milestone, QEMU |
| M11 click and drag rectangle | The rectangle follows the pointer while held | Verified software milestone, QEMU |
| M13 window object model | Stable IDs, geometry, lifetime and deterministic z-order | Verified software milestone, QEMU |
| M14 window surfaces and compositor | Window-local drawing and ordered framebuffer composition | Verified software milestone, QEMU |
| M15 focus and event queues | Focused input routed as bounded per-window events | Verified software milestone, QEMU |
| M16 dirty regions and an immediate cursor | The cursor costs 247 pixels a move instead of two million | Verified software milestone, QEMU |
| M17 tiling layout | One to eight windows, no two of them sharing a pixel | Verified software milestone, QEMU |
| M18 the ME OS Default desktop | Bars, frames, focus, keyboard control and a launcher | Verified software milestone, QEMU |
| M19 terminal and system information | A shell whose answers all come from the machine | Verified software milestone, QEMU |
| M20 filesystem and file commands | A real tree in memory, and PWD, LS, CD, MKDIR, CAT and the rest | Verified software milestone, QEMU |
| M21 editor, clock and a working shell | A text editor, the real time of day, history, MV, CP, WC and TREE | Verified software milestone, QEMU |
| M22 workspaces | More than one set of tiles, switched by keyboard | Verified software milestone, QEMU |
| M23 a disk | The filesystem is written to an ATA disk and is still there after a restart | Verified software milestone, QEMU |
| M24 files made of blocks | A file is a list of blocks from a shared pool, so a document fits in one | Verified software milestone, QEMU |
| M25 redirection and pipes | Any command can be sent to a file or piped into another | Verified software milestone, QEMU |
| M26 scrollback | Page Up and Page Down look back at what went past | Verified software milestone, QEMU |
| M27 files of commands | RUN reads a file and does what it says | Verified software milestone, QEMU |
| M28 finishing a name | Tab completes a filename, and offers nothing rather than the wrong thing | Verified software milestone, QEMU |
| M29 a page allocator | Physical memory discovered from the boot map, handed out a page at a time | Verified software milestone, QEMU |
| M30 address spaces | Page tables the kernel builds itself, and the processor runs on one | Verified software milestone, QEMU |
| M31 descriptor tables and traps | User segments, a trap stack, and a fault that is handled instead of resetting | Verified software milestone, QEMU |
| M32 user mode and system calls | A program runs at privilege three in its own address space, and a broken one costs only itself | Verified software milestone, QEMU |
| M33 ELF executables | A program that is a file on the disk, not part of the kernel, is read, mapped and run | Verified software milestone, QEMU |
| M34 windows for programs | A program opens a window of its own and draws in it, instead of printing into a terminal | Verified software milestone, QEMU |
| M35 input reaches a program | The keyboard and the mouse reach a program, and one that will not stop is stopped | Verified software milestone, QEMU |

All thirty five finished milestones are checked automatically. `make test` boots
the image headlessly, types at it, moves the mouse, opens and closes windows,
and inspects the resulting framebuffers and the kernel's own log. `make test-unit`
runs twenty five programs on the development machine with no emulator, covering
framebuffer clipping, dirty region arithmetic, mouse packet decoding, pointer
clamping, the tiling layout, the desktop, the terminal and its commands, the
processor identification, the filesystem on disk, the text editor, the clock, the page
allocator, the address spaces, the descriptor tables, the user pointer checks and the ELF reader. See
[docs/milestones.md](docs/milestones.md) for the full roadmap.

## Running it

```sh
make iso        # build build/me-os.iso
make run        # boot it in QEMU through UEFI
make run-bios   # boot the same ISO through BIOS instead
make test       # boot it headless and check what was drawn
make test-unit  # the host side checks, no emulator needed
make check-reproducible   # build it twice and compare the two files
```

## What CI checks, and what it does not

Every push and pull request runs the host suites, a clean build of the image, and
the reproducibility check. A build failure or a failing suite fails CI.

`make test` is not run there. It boots the image in QEMU, drives it with injected
key presses and mouse packets, and reads twenty framebuffer captures, and its
timing is written against a real machine rather than a shared runner. A check
that goes red for reasons nobody can reproduce teaches people to ignore the red
mark, which costs more than the check is worth. So booting stays local, and
`docs/milestones.md` remains the place that records what a person has actually
watched happen.

Nothing in CI touches a device, writes an image to a disk, or boots anything.

The image is reproducible. Two clean builds of the same source produce the same
file, byte for byte, checked by `make check-reproducible`. Everything that would
otherwise vary is pinned: the timestamps, the GPT identifier, the MBR disk
identifier Limine's installer would randomise, and the build directory, which is
mapped out of the binary by `-ffile-prefix-map`.

One ISO boots both ways. It carries an El Torito BIOS boot catalogue and an EFI
system partition, and the firmware takes whichever it understands. Two images,
one per firmware, would be two answers to what ME OS is, and the one that gets
tested is not necessarily the one that gets booted.

### VirtualBox

The same `build/me-os.iso`, in a virtual machine of its own:

```sh
scripts/vbox.sh            # create or update the machine and start it
scripts/vbox.sh capture    # start it headless, save a screenshot, stop it
scripts/vbox.sh remove     # unregister and delete the machine
```

It needs no root, writes to no real disk, and refuses to act on any machine not
named `ME-OS`. The machine it makes has 512 MB, one processor, the VBoxVGA
adapter, PS/2 keyboard and mouse, and no disk, network, audio or USB. A device
ME OS does not drive can only add a way to fail.

VirtualBox boots the BIOS half of the image and gives a 1024x768 framebuffer
where QEMU with OVMF gives 1280x800. Everything lays itself out from the
resolution it is given, so nothing about the desktop is different apart from its
size. The mouse is the same PS/2 driver in both. There is no VirtualBox specific
code anywhere in the kernel.

### What you can do once it boots

Ctrl and an arrow moves focus between tiles. Ctrl H hides the focused window and
the others grow into the space. Ctrl S brings them all back. Ctrl N and Ctrl W
move the divider between the two columns. The taskbar buttons focus a window or
bring back a hidden one, and the ME OS mark opens a small launcher.

The Terminal window takes commands. `HELP` lists them.

`CPU` asks the processor through CPUID, `MEM` adds up the memory map the
bootloader handed over, `RES` reports the resolution, `UPTIME` reads the same
clock the rectangle moves on, and `WINDOWS` counts the real windows.

`PWD`, `LS`, `CD`, `MKDIR`, `TOUCH`, `CAT`, `WRITE`, `RM`, `MV`, `CP`, `WC`,
`TREE` and `DF` move around a real filesystem, and `ECHO TEXT > FILE` writes one
from the keyboard. The up and down arrows walk back through what you have typed.
`DATE` asks the CMOS clock chip, which is also what the time in the top bar comes
from.

The filesystem is written to an ATA disk, so what you make is still there the
next time the machine starts. `DF` reports what is actually on it.

`RUN NAME` runs a file. If it begins with the ELF magic it is a program and is
loaded into an address space of its own and run at privilege three, and if it
does not it is a script and its lines are run as commands. `/BIN/HELLO` is a
real executable that the bootloader carries on the disc as its own file, so it
is not part of the kernel. `PS` says what is running.

`EDIT NAME` opens a file in the Editor window. Arrows move the cursor, typing
inserts, Enter splits a line, Backspace at the start of one joins it to the line
above, and **Ctrl O** saves. It loads a file that exists and starts a new one
that does not.

Nothing invents a process list or a network, because there are none.

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
screen and keeps its shape. Since M14 it is the compositor's final overlay
rather than an app-owned framebuffer patch. Nothing follows it and nothing can
be dragged.

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

**M9.** The arrow keys move the rectangle, sixteen pixels a press. The first
press stops it drifting, because steering something that is also wandering off
on its own is a nuisance. It moves within a corridor between the key line and
the triangle, so it cannot rub out text or part of the turning shape.

Arrows rather than WASD: since M8 every letter is part of a typed sum, so
letters would steer and type at the same time.

**M10.** Arrow-key movement wraps at the horizontal screen edges and at the
top and bottom of the safe vertical corridor. The rectangle stays whole and
on screen, and the part of a step beyond an edge is preserved at the opposite
edge. M5's time-driven movement still bounces, because M10 changes deliberate
steering rather than rewriting the earlier behaviour.

**M11.** Holding the left mouse button inside the rectangle picks it up. The
press offset is preserved, so it follows the pointer without snapping, and the
whole rectangle stays inside its safe corridor. Releasing drops it; subsequent
pointer movement leaves it behind. Clicking the background does nothing.

**M13.** Windows now exist as independent kernel objects with stable IDs,
geometry, titles, lifetime and deterministic z-order. The current kernel has no
allocator, so the manager uses a bounded pool of eight slots and fails safely
when full. IDs are not slot numbers, which keeps this temporary storage choice
out of callers.

**M14.** Windows own or reference independent, externally backed software
surfaces. Demo contains all pre-window graphics in its own local coordinates;
System is a second opaque surface above it. The compositor clears a desktop
surface, copies visible window portions from bottom to top with clipping, draws
the cursor as its own final overlay, and presents that one result to the
framebuffer. Apps no longer draw into arbitrary framebuffer regions.

Backing stores are bounded static arrays because there is still no allocator.
Attaching a surface requires an exact geometry match, and an attached window
cannot be resized implicitly. M17 will define real resize/reallocation
semantics rather than hiding that decision here.

**M15.** Every window has a bounded queue of higher-level input and focus
events. A click hit-tests the topmost visible window, focuses and raises it,
and delivers local mouse coordinates; a held pointer remains captured until
release. Keyboard input goes only to the focused window. Demo consumes only
its own queue, so the PS/2 drivers are no longer its application API.

The queue holds 32 events and explicitly drops the newest event when full,
recording the dropped count. The current keyboard decoder reliably produces
key-down events but not releases, so M15 does not invent key-up events. Clicking
the desktop clears focus, and destroying a window invalidates its queued events
and any pointer capture.

**M12.** A triangle turns about its own centre, below everything else on the
Demo surface, at a fixed speed driven by the same clock the rectangle uses. It
is drawn as three software lines: there is no graphics acceleration of any
kind and none is planned.

This is the milestone where the kernel gained floating point. x86-64 guarantees
SSE2, so the kernel enables that and nothing else, after checking CPUID for it.
Every file is still compiled with SSE off except the one that does the
arithmetic, and the build refuses to link if a floating point instruction turns
up anywhere else. Everything that file exposes takes and returns integers, so
nothing can call into it before the processor has been told to allow it.

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
                  floating point, geometry, window, event, surface, compositor,
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
