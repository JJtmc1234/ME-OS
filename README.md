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
| M4 mouse cursor | A cursor that moves with a pointing device | Next |

All three milestones are checked automatically. `make test` boots the image
headlessly, injects a key press, and inspects the resulting framebuffer.
`make test-unit` checks the framebuffer's clipping on the development machine,
without an emulator. See
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
lines of text unchanged. It is static: nothing moves it, and nothing yet reads a
pointing device.

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
kernel/include/   headers: framebuffer, font, keyboard, logging, memory
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
