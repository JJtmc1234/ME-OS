# ME OS milestones

Each milestone adds one small capability, has a clear success condition, can be
tested on its own, avoids unnecessary features, and preserves what already
works.

Status words are used carefully:

- **Verified software milestone** the success condition is met and checked
  automatically in QEMU
- **In development** being worked on now
- **Planned** written down, not started

Nothing here has been booted on a physical machine yet.

## Early milestones

| # | Milestone | Success condition | Status |
| --- | --- | --- | --- |
| M1 | Boot proof | Boots over UEFI in QEMU and displays `IF YOU SEE THIS IT WORKED` in white on black, no crash or reboot afterwards | Verified software milestone |
| M2 | Keyboard input | The M1 message is preserved and a second line reports the most recently pressed supported key | Verified software milestone |
| M3 | Draw rectangle | A filled rectangle appears at chosen coordinates in a chosen colour, without disturbing M1 or M2 | Verified software milestone |
| M4 | Mouse cursor | A cursor is drawn, moves with a pointing device, and stays on screen | Verified software milestone |
| M5 | Move rectangle | The rectangle moves across the screen over time | Next |
| M6 | Basic arithmetic | Correctly evaluates simple integer addition and subtraction and shows the result | Planned |
| M7 | Conditionals | Runs simple if/else logic and visibly demonstrates different outcomes | Planned |
| M8 | Variables | Stores and updates named values, and shows them changing | Planned |
| M9 | Keyboard controlled rectangle | Arrow or letter keys move the rectangle | Planned |
| M10 | Edge wrapping | The rectangle wraps around the screen edges instead of leaving | Planned |
| M11 | Click and drag rectangle | The rectangle can be picked up and moved with the pointer | Planned |

After M11, simple game like milestones come next, exercising input, timing and
drawing together, before any deeper operating system work such as memory
management, interrupts, processes or filesystems.

## How a milestone is judged done

1. It runs. Compiling is not passing.
2. `make test` proves it without a human watching, where that is possible.
3. Earlier milestones still work. The M1 message is checked on every run of the
   M2 test for exactly this reason.
4. A person sees it at least once. Automated framebuffer checks catch
   regressions; they do not replace looking.

## Verification status

M1, M2, M3 and M4 are verified in QEMU by automated framebuffer inspection. None has
been observed on physical ME hardware, and physical machine boot testing is a
later step that has not been scheduled.

Two kinds of test run:

- `make test-unit` checks, on the development machine and without an emulator,
  that framebuffer drawing clips at every edge, and that mouse packets decode
  and pointer movement clamps correctly.
- `make test` boots the real image headlessly, injects a key press, moves the
  mouse, and inspects the captured framebuffers: the message, the key line, the
  rectangle, and a cursor that starts in the right place, follows the mouse
  exactly, keeps its shape, and stays on screen.
