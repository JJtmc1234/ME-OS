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
| M11 | Click and drag rectangle | The rectangle can be picked up and moved with the pointer | Verified software milestone |
| M12 | Rotating triangle and floating point | SSE enabled deliberately, and a triangle turning about its own centre on a timer, drawn with lines | Verified software milestone |
| M13 | Window object model | Stable window IDs, geometry, lifetime and deterministic z-order in a bounded pool | Verified software milestone |
| M14 | Window surfaces and compositor | Window-local pixels are clipped and composited in z-order | Verified software milestone |
| M15 | Focus and event queues | Focused input is routed as bounded per-window events | Verified software milestone |
| M16 | Window chrome and dragging | Title bars, close controls and moving whole windows | Next |
| M17 | Window resizing | Bounded live resizing with explicit surface semantics | Planned |

M12 was added after M11 rather than inserted before M9, because M9, M10 and M11
were already written down and renumbering milestones that people have read is
worse than taking them out of order. It was therefore built before M9. M9 to
M11 are now complete without renumbering any of them.

The current direction is a small window system. The fixed pool in M13 and
fixed surface stores in M14 are explicit stepping stones because the kernel
has no allocator yet; building a
general heap only to claim dynamic windows would be a much larger subsystem.
Filesystem, processes, networking, audio and hardware boot remain outside this
sequence.

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

## What M11 added

The left mouse button can pick up the filled rectangle. Hit testing includes
the rectangle's drawn pixels and excludes the first pixel beyond each edge.
The point pressed is retained as an offset, so the rectangle follows the
pointer without snapping its top-left corner under it. It remains whole and is
clamped to the same safe corridor used by arrow-key steering.

Releasing the button ends the drag; later pointer movement leaves the rectangle
where it was dropped. Pressing the background starts nothing. The mouse driver
still only reports decoded device state: `main.c` translates the button edge
into the pure hit-test and drag operations in `rect.c`.

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
have actually moved. Since M14 the lines are drawn into Demo's surface rather
than directly into the framebuffer.

## What M13 added

`window.c` owns window lifetime and ordering. A window has a stable 32-bit ID,
signed position, bounded non-zero size, title, and reserved focused/minimized
state. IDs are not pool indexes: destroying a window invalidates that ID while
leaving every other ID stable, and reusing its storage slot does not
immediately reuse the ID.

New windows enter at the top of an explicit bottom-to-top z-order. Raising one
preserves every other relative order, destroying one closes only its gap, and
hit testing walks from top to bottom while skipping minimized windows. Negative
positions are valid so the compositor can later clip partially off-screen
windows safely.

There is deliberately no heap. The manager contains eight object slots and an
explicit safe failure when full. All lifetime, lookup and ordering operations
go through stable IDs, so replacing the temporary pool after a real allocator
exists does not require app or compositor callers to depend on slot addresses.
The kernel creates `Demo` and `System` objects through this path at boot; M14
gives them surfaces and makes them visible.

## What M14 added

`surface.c` provides an externally backed opaque pixel surface. Its coordinates
are local, all pixel, fill, line, text and blit operations clip safely, and
extreme off-screen fill/blit origins are bounded no-ops rather than signed
overflow. A window may attach one surface whose dimensions exactly match its
geometry. Resizing an attached window is refused because M14 does not pretend
that surface reallocation semantics already exist.

`compositor.c` clears a desktop surface and walks the manager's explicit
bottom-to-top z-order, copying each non-minimized window's visible portion.
Demo is an 1180 by 720 black surface at (40, 40) containing every earlier
visual milestone in local coordinates. System is a 300 by 180 opaque surface at
(860, 80), visibly overlapping Demo above it. The cursor is drawn last into the
composed desktop, then `fb_present` copies that result within the framebuffer's
pitch and bounds. Apps no longer write arbitrary framebuffer regions.

The three backing stores are bounded static arrays: at most 1920 by 1080 for
the desktop and exactly the current two window sizes. This is the honest
stepping stone supported by the allocator-free kernel, not a hidden heap.
Partial off-screen windows, overlap, z-order changes, minimized windows,
surface guards, extreme clipping, cursor clipping and framebuffer presentation
are covered by host tests. The QEMU run samples desktop, Demo and System
colours at their overlap and repeats every earlier visible regression across
eighteen captures.

## What M15 added

`event.c` provides a bounded 32-entry FIFO for higher-level window events.
Each window owns one queue. Overflow refuses and counts the newest event rather
than overwriting an older event silently, and destroying a window makes all of
its queued events unreachable with the same stable-ID lifetime rule as the
rest of the object.

`window.c` now owns focus and input targeting. A mouse press hit-tests from the
top of z-order, focuses and raises that window, and captures the pointer until
release. Mouse positions are translated from desktop coordinates into the
target window's local coordinates. A desktop click clears focus; keyboard
events go only to the focused window. Focus transitions are queued as
`FOCUS_LOST` then `FOCUS_GAINED`, so consumers see deterministic ordering.

The PS/2 drivers still decode only device data. `main.c` translates their
polled state into `MOUSE_MOVE`, `MOUSE_DOWN`, `MOUSE_UP` and `KEY_DOWN` events,
then Demo consumes only its own queue. There is no fabricated `KEY_UP`: the
current keyboard decoder intentionally discards releases other than the shift
state it needs internally. System has the same queue and targeting behavior
even though it has no application logic yet.

Host tests cover FIFO order, wraparound, explicit overflow, focus switching,
overlap targeting, local coordinates, keyboard isolation, pointer capture,
desktop focus clearing and destroying a window with queued input. The QEMU run
adds two captures: one after clicking System and one after clicking Demo. Pixel
samples prove Demo was raised over System, while both kernel logs record the
focus transitions to window IDs 2 then 1.

## M16 dirty regions and an immediate cursor

Done. The mouse was noticeably slow, and it was not a feeling. Every mouse
packet went through one whole screen path: clear 1,024,000 desktop pixels, blit
every window back over them, then write 1,024,000 pixels out across the graphics
adapter. Framebuffer memory sits on the far side of that adapter, so those
writes are the expensive ones, and there were two million of them per cursor
step because a move presents where the cursor was as well as where it is.

The device reports a hundred times a second and the loop consumed one packet per
pass. Once a pass cost more than ten milliseconds the controller's buffer filled
and the cursor fell further behind with every report, so the lag was not a fixed
delay but a growing one. What was on screen was a position from a packet sent
seconds earlier.

Three changes.

A region is a rectangle of screen that something has changed. Width and height
are signed, because clipping two rectangles that miss each other produces a
negative size, and an unsigned type would turn that into an enormous positive
one, which is the exact shape of an out of bounds write.

The compositor and the framebuffer both learned to work on one region.
`compositor_compose` is now `compositor_compose_region` over the whole target,
and `fb_present` is `fb_present_region` over the whole surface, so there is one
composition rule and one copying loop rather than two that could come to
disagree about an edge. A test composes a scene whole and again as overlapping
patches and compares every pixel.

The input loop drains every packet the controller is holding instead of one.
Buttons are still handled packet by packet with the pointer where it was when
that packet arrived, so a click and its release inside one batch stay two events
in the right order at the right places. Only the drawing waits for the end of
the batch, which means a backlog collapses into one step rather than being
replayed at the speed of the display.

The cursor is composed from the window surfaces every time rather than from a
saved copy of what was underneath it. Saving and restoring is faster still and
is wrong the moment a window repaints under the cursor, which would stamp stale
content wherever the cursor had been.

Measured, not asserted. Both versions were built and run through the same
`make test`, and the kernel counts its own work:

| | whole screen path | dirty regions |
|---|---|---|
| cursor updates | 24 | 24 |
| pixels written for them | 49,152,000 | 5,932 |
| per cursor movement | 2,048,000 | 247 |
| total pixels presented | 14,558,208,000 | 225,781,066 |

That is 8,290 times less work for one cursor movement, and 64 times less for
the run as a whole, because the turning triangle was presenting the whole screen
on every step too and was starving the mouse alongside its own animation.

`tests/check_boot.py` now reads those counters back and fails if a cursor
movement costs more than 8,000 pixels or the whole screen is composed more than
sixteen times in a run. The number that matters is not 247 against 300, it is
247 against two million. Checked by putting the whole screen path back, which
fails the check with 2,048,000.

## M17 tiling layout

Done. ME OS Default is tiling first. A normal window does not choose where it
sits: it is given a tile, told the size of the area inside its frame, and asked
to paint that. Opening one, hiding one or closing one recalculates every tile,
so no two visible windows can overlap by construction rather than by a z-order
that happens to keep them apart.

One rule rather than a table of cases. Two columns, the left taking the master
percentage of the width and holding half the windows rounded down, the right
holding the rest, each column splitting its height evenly. One window fills the
workspace, two sit side by side, three put one on the left and two on the right,
four make a two by two grid. A table of special cases is where the case nobody
drew goes wrong.

A tile that cannot meet the minimum size is not placed at all rather than placed
too small, so asking for more windows than the screen can hold gives fewer areas
back and says so.

The property that matters is checked as a property, not as four pictures. One to
eight windows on five screen sizes, and no pair of tiles shares a pixel, and none
reaches into a bar. Checked by pulling the right column four pixels left, which
fails at every count from two upwards.

Tile resizing moves the divider rather than dragging a corner, which is what
resizing means in a tiling layout. Ctrl N and Ctrl W move it five percent at a
time between twenty and eighty.

## M18 the ME OS Default desktop

Done. A top bar with the ME OS mark, the workspace, what has focus and the
uptime. A taskbar with a launcher and one button per window. Tiles between them,
with a small title strip, a hide button, a close button and a border that is the
accent when the window has focus and dim when it does not.

The focused border is the only thing that changes with focus, deliberately. It
is what says which window the keyboard is talking to, and it does not need help
from a differently coloured title or a heavier frame. It is also the only thing
repainted on a focus change, so moving focus does not wipe every app's content
to recolour four thin strips.

Each window has one surface covering its whole tile and a client view onto the
part inside the frame. The view shares the frame's pixels through the stride, so
an app drawing in its own coordinates draws into the tile with no second buffer
and no second blit, and cannot paint over its own frame.

The tiles share one backing store rather than each having a private one. Tiles
never overlap, so their areas together are never more than the workspace, and
four private stores each big enough to be the only window cost four times the
memory for a case that cannot happen. The first version did have four, and the
zeroing at boot was long enough to see: the screenshot the boot test takes landed
before the cursor was drawn.

The bars are painted after composition rather than before, because the compositor
clears what it composes, and only when the region being presented actually
reaches one.

Control is the modifier, not Super. The keyboard would decode Super perfectly
well. Neither QEMU nor VirtualBox reliably delivers it, because the host's own
window manager takes it first, and a shortcut that works here and silently does
nothing on the next machine is worse than a different shortcut.

Ctrl and an arrow moves focus. Ctrl H hides the focused window and the others
grow into the space. Ctrl S brings every hidden window back. The last visible
window is not allowed to hide, because a desktop with nothing on it and no way
to get anything back is not a state worth reaching with one key.

## M19 a terminal, and a machine that knows what it is

Done. The thing that makes a machine feel like a computer rather than a picture
of one is being able to type at it and have it answer.

A character grid with scrollback and a line editor. The scroll happens when a
character is written to a row past the end, not when a newline is typed: a
newline only says the cursor has left this row, and scrolling eagerly meant the
newest output was never on the bottom row where a person looks for it.

What the commands mean is a separate file. A terminal that also knew what HELP
meant could not be tested without testing every command at the same time.

Every command reports something the kernel actually looked up. CPU asks CPUID.
MEM adds up the memory map Limine handed over. RES is the resolution Limine
chose. UPTIME is the same clock the rectangle moves on. WINDOWS counts the real
windows. Nothing invents a filesystem, a process list or a network, because there
are none, and a shell that answers questions the machine cannot answer is a mock
up. A machine that could not be asked says so: MEM with no memory map prints that
rather than a zero.

Two things this found. The font had no lowercase, so "AuthenticAMD" came back as
a row of boxes. And adding every memory map entry reported a machine with half a
gigabyte as having twelve, because the hole between the top of RAM and the
devices is enormous and is address space rather than memory.

The title bar buttons, the taskbar buttons and the launcher all do something. A
click on the desktop is offered to the window manager first, because the launcher,
the taskbar and a tile's own title strip belong to it and not to the app inside
the tile. Close puts a window away rather than destroying it, and says so in the
log, because nothing here can build one again from nothing and a close that lost
an app for the rest of the run would be worse.

## How a milestone is judged done

1. It runs. Compiling is not passing.
2. `make test` proves it without a human watching, where that is possible.
3. Earlier milestones still work. The M1 message is checked on every run of the
   M2 test for exactly this reason.
4. A person sees it at least once. Automated framebuffer checks catch
   regressions; they do not replace looking.

## Verification status

M1 to M19 are verified in QEMU by automated framebuffer inspection. None has
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
  boundaries. An eighth program covers window IDs, object lifetime, geometry,
  capacity, z-order, hit testing, focus and input routing. A ninth covers local
  surfaces, clipping, cursor overlay, composition, overlap and presentation
  guards. A tenth covers event order, circular queue behavior and explicit
  overflow.
- `make test` boots the real image headlessly, injects a key press, moves the
  mouse, types two sums, two conditionals, an assignment and two lines that use
  what it stored, steers the rectangle across two edges, drags and releases it,
  changes focus twice, and inspects twenty captured framebuffers: the sum line,
  the message, the key line, a rectangle that is whole, on screen and in a
  different place each time, and a cursor that starts in the right place,
  follows the mouse exactly, keeps its shape, and stays on screen.
  It also samples the desktop and two opaque overlapping windows to prove M14's
  local surfaces and bottom-to-top composition, then samples before and after
  click-to-raise to prove M15's focus and targeting path.
