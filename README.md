# ME OS

ME OS is a from-scratch x86-64 operating system project.

The long-term goal is to build a general-purpose operating system with:

* strong Linux/Unix compatibility where practical
* standard file formats and application conventions
* reliability and recovery as core features
* agent-native computing
* an agent-centric desktop
* eventual integration with Carl and the wider ME ecosystem

This project starts extremely small and advances through many individually testable milestones.

## Current milestone

### M1: Boot proof

The system must boot successfully in a virtual machine and display:

```text
IF YOU SEE THIS IT WORKED
```

Requirements:

* x86-64
* UEFI boot
* black background
* white text
* no crash or reboot after displaying the message
* no additional functionality required

M1 is complete when this works reliably in QEMU.

## Planned early milestones

* M1: Boot and display the success message
* M2: Keyboard input
* M3: Draw a rectangle
* M4: Mouse cursor
* M5: Move the rectangle
* M6: Basic addition and subtraction
* M7: Basic conditional logic
* M8: Variables
* M9+: Continue in small, testable steps

The project is expected to pass well over 100 milestones before approaching the capabilities of a modern desktop Linux distribution.

## Development philosophy

Each milestone should:

1. Add one small capability.
2. Have a clear success condition.
3. Be testable independently.
4. Avoid unnecessary features.
5. Preserve working behavior from previous milestones.

Do not skip ahead merely because a later feature is interesting.

## Architecture direction

Initial target:

* Architecture: x86-64
* Firmware: UEFI
* Bootloader: Limine
* Kernel: custom, freestanding
* Initial implementation language: C
* Initial test environment: QEMU

The architecture may evolve as the project grows.

## Compatibility direction

ME OS should reuse existing Linux and Unix standards where practical instead of inventing proprietary formats unnecessarily.

Long-term compatibility goals may include:

* ELF executables
* conventional filesystem paths
* common Linux file formats
* standard MIME types
* common image, audio, video, and document formats
* Linux-style application metadata
* POSIX-like interfaces where useful
* compatibility paths for existing Linux applications

ME-specific formats should only exist where they provide a real advantage.

## Testing strategy

Development order:

1. Build the milestone.
2. Test it in QEMU.
3. Fix all known milestone-blocking errors.
4. Re-test repeatedly.
5. Later produce a bootable USB.
6. Test on spare physical hardware without installing to or modifying the internal disk.

Real hardware testing should initially use USB boot only.

## Scope rule

For M1, do not implement:

* keyboard input
* mouse input
* filesystems
* networking
* multitasking
* user accounts
* shells
* applications
* agents
* Carl integration
* package management
* audio
* USB support beyond anything inherently handled before the kernel runs

If the screen displays `IF YOU SEE THIS IT WORKED`, M1 has done its job.

## Repository

Expected early structure:

```text
ME-OS/
├── kernel/
│   ├── src/
│   └── include/
├── boot/
├── scripts/
├── docs/
├── tests/
├── limine.conf
├── linker.ld
├── Makefile
└── README.md
```

This structure is intentionally provisional. Keep it simple until the project actually needs more organization.

## Rule zero

Make it boot first.

Everything else is somebody else's milestone.
