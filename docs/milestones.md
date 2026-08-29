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
| M5 | Move rectangle | The rectangle moves across the screen over time, staying whole and on screen | Verified software milestone |
| M6 | Basic arithmetic | Evaluates whole number arithmetic typed on the keyboard and shows the result | Verified software milestone |
| M7 | Conditionals | One conditional expression, IF a compared to b THEN x ELSE y, taking either branch | Verified software milestone |
| M8 | Variables | Stores and updates named values, and shows them changing | Verified software milestone |
| M9 | Keyboard controlled rectangle | The arrow keys move the rectangle, which stops drifting once it is being steered | Verified software milestone |
| M10 | Edge wrapping | The rectangle wraps around the screen edges instead of leaving | Verified software milestone |
| M11 | Click and drag rectangle | The rectangle can be picked up and moved with the pointer | Next |
| M12 | Rotating triangle and floating point | SSE enabled deliberately, and a triangle turning about its own centre on a timer, drawn with lines | Verified software milestone |

M12 was added after M11 rather than inserted before M9, because M9, M10 and M11
were already written down and renumbering milestones that people have read is
worse than taking them out of order. It was therefore built before M9, and M10
is next, so the ladder is still walked in order from here.

After M11, simple game like milestones come next, exercising input, timing and
drawing together, before any deeper operating system work such as memory
management, interrupts, processes or filesystems.

## What M9 added

The arrow keys move the rectangle sixteen pixels at a time.

Arrows rather than letters, because since M8 every letter is part of a typed
sum: WASD would steer the rectangle and type into the calculator at the same
time. Arrows arrive with an extended prefix, which the keyboard decoder used to
throw away, so it now decodes those four and still discards the rest of that
set. They are named keys rather than printable ones, so they cannot end up in a
sum by another route.

The first arrow press also stops the rectangle drifting. Steering something
that is wandering off on its own is a nuisance, and it makes the movement exact
rather than approximate, which is what lets the test measure it in whole steps.

Vertically it is held in a corridor between the key line and the triangle.
Nothing else lives in that band, so steering cannot rub out text or part of the
shape that turns. Widening it would need something that can repaint whatever
was underneath, and no milestone has asked for that.

Picking the rectangle up with the pointer is M11. It is not part of M9.

## What M10 added

Arrow-key movement wraps at all four ends of the rectangle's safe corridor.
The whole rectangle always remains visible: crossing the left or right edge
reappears at the opposite horizontal edge, and crossing the top or bottom
reappears at the opposite end of the vertical corridor. Any part of the
sixteen-pixel step beyond the edge is preserved, so repeated steps stay evenly
spaced rather than sticking to an edge for one press.

This applies only to deliberate arrow-key movement. M5's time-driven motion
still reflects at the horizontal edges, preserving that earlier milestone's
behaviour. The wrap arithmetic takes constant time even for a very large step
and is exercised with the largest signed inputs by the host tests.

## What M12 added

Floating point, and the smallest thing worth doing with it.

x86-64 guarantees SSE2, so that is what the kernel uses. The processor starts
with floating point disabled, so `fpu.c` checks for it with CPUID and then
clears CR0.EM, sets CR0.MP, and sets CR4.OSFXSR and CR4.OSXMMEXCPT. x87 is not
used at all and neither is AVX: one way of doing floating point is enough, and
mixing them is how state gets corrupted.

The whole kernel is still compiled with SSE off, except `geometry.c`. That is
the only file allowed to do arithmetic, and everything it exposes takes and
returns integers, so no other file can even pass it a double: the calling
convention would put one in a register that file is not allowed to touch. The
build checks this rather than trusting it, by refusing to link if any other
object contains an SSE instruction.

`geometry.c` has a sine and a cosine, an angle that advances with elapsed time,
a rotation about a point, and rounding to whole pixels. No matrices, no
vectors, no general maths library. The sine and cosine reduce the angle into a
quadrant and then use a six term series, which is accurate to about 1e-12,
measured against the host library across a whole turn.

`fb_draw_line` draws the edges with Bresenham, in integers, clipped like every
other write to the framebuffer.

The triangle sits below everything else and turns at 0.6 radians a second on
the same clock the rectangle uses. It is redrawn only when its rounded corners
have actually moved.

## How a milestone is judged done

1. It runs. Compiling is not passing.
2. `make test` proves it without a human watching, where that is possible.
3. Earlier milestones still work. The M1 message is checked on every run of the
   M2 test for exactly this reason.
4. A person sees it at least once. Automated framebuffer checks catch
   regressions; they do not replace looking.

## Verification status

M1 to M10 and M12 are verified in QEMU by automated framebuffer inspection. None has
been observed on physical ME hardware, and physical machine boot testing is a
later step that has not been scheduled.

Two kinds of test run:

- `make test-unit` checks, on the development machine and without an emulator,
  that framebuffer drawing clips at every edge, that mouse packets decode and
  pointer movement clamps correctly, that timer wrap arithmetic and the
  rectangle's movement hold over long runs, that scancodes translate with and
  without shift, that arithmetic and the one conditional get the right answer
  and refuse overflow, division by zero, fractional powers and malformed input,
  and that the variable table stores, overwrites, refuses a name too many and
  leaves itself alone when a line is refused. Seven host programs cover these
  boundaries.
- `make test` boots the real image headlessly, injects a key press, moves the
  mouse, types two sums, two conditionals, an assignment and two lines that use
  what it stored, steers the rectangle across two edges, and inspects fifteen
  captured framebuffers: the sum line,
  the message, the key line, a rectangle that is whole, on screen and in a
  different place each time, and a cursor that starts in the right place,
  follows the mouse exactly, keeps its shape, and stays on screen.
