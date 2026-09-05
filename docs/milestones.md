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
| M16 | Dirty regions and an immediate cursor | Only what changed is composed and presented, and a cursor move costs a few hundred pixels rather than a screen | Verified software milestone |
| M17 | Tiling layout | Windows are placed by a rule into tiles that never overlap and never reach into a bar | Verified software milestone |
| M18 | The ME OS Default desktop | Bars, frames, focus and a taskbar, with click to focus routed to the right window | Verified software milestone |
| M19 | A terminal, and a machine that knows what it is | A shell that answers with what the kernel actually measured, never a written down answer | Verified software milestone |
| M20 | A filesystem, and the commands that move around it | Real directories, files and paths in memory, with real errors when a path is wrong | Verified software milestone |
| M21 | An editor, a clock, and a shell you can work in | Text can be changed in the middle of a line and saved, and the machine knows the time | Verified software milestone |
| M22 | Four workspaces | Ctrl 1 to 4 switch and Ctrl M sends a window across, and a window off screen draws nothing | Verified software milestone |
| M23 | A disk | The filesystem is written to an ATA disk and read back, and survives a restart | Verified software milestone |
| M24 | Files made of blocks | A file is a list of blocks from a shared pool, so a document fits in one | Verified software milestone |
| M25 | Output that need not go to the screen | Any command can be redirected to a file or piped into another | Verified software milestone |
| M26 | Scrollback | Page Up and Page Down look back at what went past, two hundred lines of it | Verified software milestone |
| M27 | Files of commands | RUN reads a file and does what it says, and a script that runs itself stops | Verified software milestone |
| M28 | Finishing a name | Tab completes a filename, and offers nothing rather than the wrong thing | Verified software milestone |
| M29 | A page allocator | Physical memory is discovered from the boot map and handed out one page at a time, and no page is ever handed out twice | Verified software milestone |
| M30 | Address spaces | Page table trees the kernel builds itself, so two of them can mean different memory by the same address, and the processor runs on one | Verified software milestone |
| M31 | Descriptor tables and traps | The kernel's own segment table with user segments and a trap stack, and 256 interrupt vectors, proved by taking a fault and returning from it | Verified software milestone |
| M32 | User mode, processes and system calls | A program runs at privilege three in its own address space, writes through a system call, and exits, and a broken one is stopped without taking the machine | Verified software milestone |
| M33 | ELF executables | A program that is a file on the disk, not part of the kernel, is read, checked, mapped and run by typing RUN | Verified software milestone |

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

## M20 a filesystem, and the commands that move around it

Done. PWD, LS, CD, MKDIR, TOUCH, CAT, WRITE, RM and DF, and `ECHO TEXT > FILE`
to write one from the keyboard.

None of it is a mock up. There was no disk driver when this was built, so there
was nothing for a filesystem to sit on, and the answer was to build a real one in
memory rather than to print what a filesystem would have said. M23 put a disk
under it. It has real directories, real
files, real path resolution with `.` and `..`, and real errors when a path is
wrong. It is what a machine has before it has a disk.

What it does not do, it says. DF ends by saying that none of it survives a
restart, every time, because that is the one thing about this filesystem that
will surprise somebody who has used another one.

One fixed block per file, held inside the node, rather than an allocator. A bump
allocator would leak on every rewrite and a real one is a milestone of its own,
whereas a fixed block cannot fragment, cannot leak and cannot be got wrong. The
cost is that a small file takes as much room as a large one, which matters when
there is a disk and does not matter yet.

Three things the tests are really for. A path made of enough `..` has to stop at
the root rather than walking off the front of the node table, so the root is its
own parent. Unlinking has to leave the sibling list joined up, checked by
removing the middle of three and then the first. And the directory you are
standing in must not be deletable, because that would leave every relative path
after it resolving into a node nothing points at. Each was checked by breaking
it: without the last guard, four checks fail.

A file that is given more than it can hold is refused rather than cut. A file
that quietly held less than it was given is a file whose contents nobody can
trust, which is worse than one that will not take them.

The shell found a real gap in the keyboard. It listed README.TXT and then could
not be told to open it, because full stop was only decoded with shift held,
where it is a greater than sign. Comma, full stop and underscore are now keys.

## M21 an editor, a clock, and a shell you can work in

Done. The filesystem could be listed and walked, and nothing could change the
middle of a line in it, which is most of what anybody does with files. This is
the milestone that makes it an environment rather than a demonstration.

**The editor.** Lines you can move around in and change, with a cursor,
scrolling, and Ctrl O to save. `EDIT NAME` from the shell opens it, loads the
file if there is one and starts a new one if there is not, and puts the window
in front of you.

The parts worth testing are the ones a person notices in a minute and a compiler
never will. Typing in the middle of a line has to insert rather than overwrite.
Enter has to leave the head of the line where it was and take the tail down with
it, and getting that backwards puts the tail of every line above its own head,
which is what the test checks by breaking it. Backspace at the start of a line
joins it to the one above. Moving down from a long line onto a short one has to
bring the cursor in, or the next character is written past a terminator.

A document that will not fit the file is refused rather than truncated. Saving
part of a document over the whole of one is the worst thing an editor can do.

**The clock.** The machine has known the date since it was switched on and
nothing had ever asked it. The CMOS chip reports either binary or binary coded
decimal, and reading one as the other gives a clock that is right for the first
ten minutes of every hour. The afternoon bit is in the top of the hour byte, and
midnight is twelve rather than zero, so adding twelve to both puts midnight at
noon. Both are checked against values from the datasheet.

The registers are read twice and only a matching pair is trusted, because the
chip can tick between the first register and the last and report an hour that
never happened. A reading that is impossible is thrown away and the bar shows
the uptime instead, because a wrong clock is worse than a missing one: nothing
downstream can tell it is wrong.

**A shell you can work in.** Command history on the arrows, MV, CP, WC and TREE,
and DATE. Moving a directory inside itself is refused, because that takes the
whole subtree out of the tree and leaves it pointing at its own parent where
nothing can reach it again.

**Apps can have shortcuts now.** The window manager used to swallow every
control combination, which meant no app could ever have one. It now takes only
the ones it uses and passes the rest to whatever has focus, which is how Ctrl O
reaches the editor.

## M22 four workspaces, and the bug that made them necessary

Done. Ctrl 1 to Ctrl 4 go to a workspace. Ctrl M sends the focused window to the
next one and leaves you where you are, because moving a window away and
following it are two different wishes, and only the first can clear a screen.

Five windows on a 1024 by 768 screen is five tiles none of which is big enough
to work in. A tiling desktop answers that with workspaces rather than with
overlapping windows, and this is that answer.

**A workspace is a different kind of absent from a hidden window.** Hiding is
about this screen. A workspace is about which screen you are looking at. Both
end up meaning "not drawn" to the compositor, so both set the same flag, and one
function, `desktop_on_screen`, is the only place that works the answer out.
Before this there were four places asking `app->hidden` directly and each one
would have needed the workspace test adding to it.

**The top bar shows every workspace, not only the one you are on.** The one you
are looking at is the accent colour and carries a line under it. One with
windows on it is readable. An empty one is dim. Where your work is takes no
clicking to find out, and the underline means it still reads correctly in a
photograph or to somebody who cannot pick the colour out.

**The taskbar shows every window on every workspace**, dimmed when it is not on
this screen. That is what made it safe to allow an empty workspace, which used
to be refused: a desktop with nothing on it and no way back is not a state worth
reaching with one key, and now there is a way back.

**Focus settles in one place.** `desktop_relayout` is the only function that
knows which windows are on the screen after it runs, so it is the only one that
moves focus. Switching workspace, hiding a window and moving one all change that
set, and each deciding focus for itself is three chances to leave the keyboard
talking to a window nobody can see.

**The bug.** The tiles share one arena, handed out in layout order. That is what
makes them cheap and it is why they cannot overlap. It also means the slice a
window held is given to a different window the moment it leaves the screen, and
an app that kept drawing into its old surface was writing into somebody else's
tile. It showed up as one window's picture smeared across a third one, which is
what the screenshot showed.

The fix is to empty the surface rather than to remember not to draw. An emptied
surface fails `surface_valid`, so every drawing call on it becomes a no
operation. That is one guard in one place instead of a check every app has to
remember, and forgetting it is exactly what happened.

The test does what the app was doing. It draws through every surface call an app
could make on a surface it no longer owns, then checks the window that stayed is
pixel for pixel as it was. Without the fix it fails.

## M23 a disk, so the machine remembers

Done. There is an ATA driver, an on disk format, and a filesystem that is still
there after the machine restarts. DF has been apologising for this since M20 and
now it has something else to say.

**The format is written a byte at a time.** A header sector, then one record per
node at sector `1 + 2 * i`. Not a copy of the structures, because a C structure
has padding the compiler chooses and a disk written by one build would be read
crooked by the next. Every number is little endian at an offset the source
names, and node records are two sectors when a node needs less than one and a
half, because simple arithmetic is worth more here than density. Anybody can
check `1 + 2 * i` against a hex dump when something has gone wrong.

The header records the limits the filesystem was built with. A disk written by a
build with a different node count is refused rather than read as though the
fields were where this build expects them, which would read one file's contents
as another file's name.

**A free node is written as nothing at all.** Deleting a file marks its node free
without clearing it, so its old contents are still in memory. Writing them out
would put deleted files on the disk where somebody could read them back. The
test writes a file, deletes it, saves, and searches the whole disk image for the
text.

**Nothing off a disk is believed until the shape has been checked.** This is the
half that matters. Every walk in `vfs.c` follows `parent`, `first_child` and
`next_sibling` without checking them, which is right, because nothing inside the
kernel can make them wrong. A disk can. A parent pointing at its own child makes
`vfs_path_of` loop forever and a first child of five thousand reads past the end
of the node table. Neither shows up as a wrong answer. Both take the machine
down.

So `vfsdisk_sound` checks the whole arrangement before anything walks it: every
link in range and in use, every parent a directory, no loop in the parents, no
loop in a sibling list, and every node listed exactly once by the parent it
claims. The test breaks one field at a time, twelve ways, and each one has to
be refused.

**A failure that touched nothing leaves the filesystem alone.** A disk that will
not answer at all is not a reason to empty a working filesystem, and finding out
which of the two had happened is what the second version of that test was for.
Once reading has started, any failure leaves it empty rather than half read,
because half a filesystem looks exactly like a whole one.

**Saving happens by itself.** `struct vfs` counts changes, every operation that
succeeds moves the counter and every one that is refused does not, and the shell
writes the disk when the number moved. There is no SYNC to remember. A machine
that loses an afternoon's work because you did not type a magic word is a machine
nobody should have to learn.

**The machine QEMU gives you does not have an IDE controller.** The q35 chipset
has AHCI and nothing at 0x1F0, which the driver found out by looking: the first
boot with a disk attached reported no disk at all. The answer was not to change
the machine, which would have meant retesting M1 through M22 on different
hardware. It was `-device isa-ide`, which hangs a legacy controller off the LPC
bridge q35 already has. Same machine, same ISO, and a disk at the ports the
driver knows.

VirtualBox needed nothing special. Its IDE controller is real, and the disk goes
on the secondary channel so it is not sharing a cable with the CD.

**Every wait in the driver is bounded.** A controller that is not there leaves
the bus floating and every read comes back as 0xFF, which has the busy bit set.
An unbounded wait for "not busy" would hang the kernel before it drew anything,
on every machine without an IDE controller. Asking is allowed to fail. Hanging is
not.

**The test boots twice.** The first run writes a file and the disk is blanked
before it, so nothing is left over from last time. The second run is handed the
ISO and that disk and nothing else, and has to report the file in the listing it
prints when it loads. The check also fails if the first boot loaded anything,
because that would mean the disk was not blank and the second boot proved
nothing.

## M24 files made of blocks, so a document fits in one

Done. A file was five hundred and twelve bytes. It is six thousand now, and the
reason that number is six thousand rather than a round one is the point of the
milestone.

**The editor could hold more than a file could take.** Forty eight lines of a
hundred and ten characters is a little over five thousand, and a file held five
hundred. You could type a page, press Ctrl O, and be told that none of it could
be saved. Refusing was the right thing to do with what it had. The right fix was
to make the two agree, and there is now a static assertion in `editor.h` that
fails the build if they ever stop agreeing.

**Fixed blocks, not a heap.** Every block is the same size, so nothing here can
fragment: a free block always fits whatever wants one. That removes the whole
class of bug where a filesystem with room left refuses a file because none of
the free space is in one piece. The cost is that a file of one byte still uses a
whole block, which is a cost worth paying to delete a category of failure.

**Direct blocks only.** Twelve block numbers held inside the node, and no
indirect block. Twelve reaches six kilobytes, which is more than the editor can
hold, and an indirect block would add a second way to lose a file for a size
nothing here can produce.

**A bitmap, not a free list, and the bitmap is not on the disk.** A free list is
a chain, and a chain on a disk is one wrong number away from a loop that never
ends or a block handed out twice. Which blocks are spoken for is worked out from
what the files claim, every time a disk is read, because a bitmap on the disk
would be a second answer to a question the files already answer. When two
answers disagree, the machine hands the same block to two files and each writes
over the other.

**All or nothing.** A file that asked for three blocks and got two has a hole in
the middle of it, and nothing downstream can tell. So the blocks are taken into
a list first and written into the node only once every one of them came back,
and a request the pool cannot cover puts every block back and leaves the file
exactly as it was. Removing the rollback and running the tests fails two checks,
which is how that is known to be tested rather than merely written.

**Two files naming the same block is the new way to be wrong.** No amount of
checking the tree would catch it, because the tree is perfectly good. It is
caught where the bitmap is built, along with a block number past the end of the
pool, a gap in the middle of a file, a file holding fewer blocks than its length
needs, and a directory holding one at all.

**A free node is written to the disk as zeros**, and zero read back as a block
number is block zero rather than no block. Without one line in `read_node` every
free node on a disk would come back claiming the first block, and the check that
no two owners share one would then refuse every disk ever written.

**Blocks are cleared when handed out, not when given back.** A block still
holding the last file's bytes would show them through the tail of a file that
was never written that far, and the length is the only thing standing between
that and being read.

**The disk format is version two.** A node used to carry its contents and needed
two sectors. It carries twelve block numbers now and fits in well under one, so
nodes are one sector each and blocks follow them, one to a sector, because a
block is exactly a sector. A version one disk is refused by version rather than
read as though the fields were where this build expects them.

**The boot test now checks a file that spans blocks.** The starting filesystem
holds a guide that is deliberately longer than one block, and the log names the
largest file with a position sensitive checksum of it. The run before the
restart and the run after it have to agree. A block count alone would not do:
a file whose two blocks came back the wrong way round is exactly as long as one
that did not.

## M25 output that does not have to go to the screen

Done. Any command can be written to a file, any command can be handed to
another, and the filters that make that worth doing are here: GREP, HEAD, TAIL
and SORT.

`LS | SORT > NAMES.TXT` works. So does `CAT NOTES | GREP TODO`.

**The whole milestone is one change.** Every command used to call
`term_println` directly, so its output could only ever go to the terminal. That
is why `ECHO HI > NOTES` was the only redirection the shell had: ECHO was the
one command whose output the shell could work out for itself without running
it. The comment in `cmd.c` said so, and said what it would take.

A command now writes to a `struct cmd_out` and does not know what is on the
other end of it. The shell points it at the terminal, or at a buffer, and that
one change is what makes the arrow and the bar both work for every command
without any command knowing either exists.

**Two buffers are enough however long the pipeline is.** A stage reads one and
writes the other, and the one it read is free the moment it has finished. They
are static rather than on the stack, because twelve kilobytes is more than a
kernel stack should be asked for and nothing here runs twice at once: there is
one terminal and it runs one command at a time.

**The arrow is taken off before the bars.** It applies to the whole pipeline
rather than to the last stage of it, so `LS | GREP TXT > FOUND` writes what came
out of the far end, which is what it looks like it should do.

**One trailing newline comes off what lands in a file.** Every command ends its
last line, so captured output always has a newline nobody asked for. This
filesystem holds a file as lines with nothing after the last one, which is what
WRITE puts in and what CAT expects to find. Keeping it would show a blank line
every time the file was read. Only one comes off, so a file that really does end
in a blank line still can.

**CLEAR reaches past the sink on purpose.** It is the one command that is about
the screen rather than about output, so there is nothing for it to write, and
piping it does nothing, which is right.

**A filter with no filename reads what came down the pipe.** That is what makes
`GREP TXT NOTES.TXT` and `LS | GREP TXT` the same command asked the same
question about text from two places. A name always wins over the pipe, which is
what every other shell does. Given neither, it says so rather than printing
nothing.

**The keyboard could not type a bar.** Scancode 0x2B was in neither table, so
the backslash key did nothing at all, and a shell that understands `A | B` with
no way to type the bar understands nothing. The key is decoded now, both
characters have glyphs, and the boot test types a real pipe at a real keyboard
rather than trusting that it would work.

**An arrow with nothing usable either side of it is reported.** It used to fall
through and run the line as it stood, which took the arrow itself as an argument
and reported a file called `>`.

**The shell is six files now.** Cutting a line up, the machine's own answers,
the dispatcher and its pipeline, files, text filters, and somewhere to write.
`cmd.c` had reached six hundred lines, which is the same thing that happened to
`vfs.c` and has the same answer.

## M26 scrollback, which the header had been claiming since M19

Done. Page Up and Page Down look back at what went past, two hundred lines of
it.

**The header said the terminal had scrollback and it did not.** What it had was
scrolling, which is close to the opposite: a line reaching the top was written
over and gone. The comment even explained that the grid scrolls "which is what
scrollback is", and that is not what scrollback is. Run TREE on anything with a
few directories in it and the start of the answer could not be got back. HELP
was longer than the tile it printed into.

**A ring, not a list.** It fills up and keeps going, dropping the oldest line to
make room, which is what makes the buffer a size rather than a limit somebody
meets and has to think about.

**At the bottom it costs nothing.** `termback_row` hands back the grid itself
when the view has not moved, which is where a terminal spends nearly all of its
life. Nothing pays for scrollback until somebody uses it.

**A page is a screen less one line.** The line you were reading at the edge is
still on screen after the jump. A whole page leaves nothing in common between
the two views and makes it easy to lose your place.

**Output arriving while you are reading does not drag you along.** The view
holds the same text rather than the same line number, so a line arriving at the
bottom does not pull what you are reading upwards. Taking that out fails a
check, which is how it is known to be tested rather than merely written.

**The prompt says where you are.** A terminal showing old output with a live
prompt under it looks like a machine that has stopped answering, so while the
view is back the prompt line says how far and which key returns.

**Typing puts you back at the bottom**, because reading the past is worth doing
and typing into a screen that is not showing what you type is not. Clearing the
screen returns the view too, but keeps what was kept: CLEAR empties the screen,
it does not burn the past.

**The keys had to be decoded first.** Page Up and Page Down were in the part of
the extended scancode set the keyboard dropped, so a scrollback nothing could
reach would have been the same as no scrollback. They are named keys rather than
characters, so they can never land in a sum or a filename, and the boot test
presses the real keys and reads the kernel's own report of where the view went.

## M27 files of commands, which is where a machine stops being a demonstration

Done. `RUN SETUP.TXT` reads a file and does what it says, one line at a time.

The machine could make files, change them, search them, sort them and keep them
through a restart. It could not do anything with one except read it back. This
is the difference between a filesystem and a thing you can teach.

**A script is a file of the same lines you would type.** No variables, no loops,
no conditions. Those are a language, and a shell that grows one by accident
grows it badly. What this adds is writing down a sequence you do often and
running it again, which is what most scripts anybody actually writes are. Blank
lines and lines starting with `#` are skipped, so a script can say what it is
for.

**Everything already in the shell works inside one.** A script line can pipe and
can redirect, because RUN hands each line to the same `cmd_run` the keyboard
does. There is no second, smaller shell inside the first one, which is where
this sort of thing usually goes wrong.

**And that is the whole difficulty.** `cmd_run` now calls itself, and it was
never written to be called twice at once.

**The pipeline buffers are per depth.** A pipeline captures its stages into two
buffers. With one pair shared, a script whose output is being captured, running
a line that has its own pipe, starts a second capture into the buffer the first
one is still filling. The outer buffer is emptied halfway through and the only
sign of it is a short file. Nothing reports an error. There is a test for
exactly that shape, `RUN NESTED.TXT > OUT.TXT` where the script has a pipe in
the middle of it, and sharing the buffers fails it.

The first version of that test did not catch it. It nested two scripts, which
looks like the dangerous case and is not: the inner one finishes before the
outer one's next line begins, so their buffers never overlap in time. The
collision needs the outer stage to still be mid capture, which takes a redirect
on the outside and a pipe on the inside. Mutation testing is what found that the
first test proved nothing.

**A script that runs itself stops and says so.** Four deep is the limit, checked
in two places: in RUN, so the file is not even read, and in `cmd_run`, which is
the one that actually protects the stack. Taking the guard out and running the
tests does not fail a check, it crashes the test program, which is what it would
do to the machine. There is no memory protection here. A stack that runs out is
not an error message.

**The keyboard gained the minus key in the test harness**, which is not the
kernel's fault: the decoder had known it since M20 and the script that types at
QEMU did not know its name.

**The drag test was flaky, and the flake was worth chasing.** It failed twice
saying the held pointer had moved 175 pixels instead of 180. Reading the cursor
out of the captures showed it starting at x=175 and ending at x=0: the drag had
run into the edge of the screen and lost the difference.

The first guess was that the mouse was losing packets, which would have been a
real fault and the one thing this project cares most about. It is not: with the
aim fixed the cursor lands on exactly the pixel it was aimed at, 1015 of 1015.
`mouse_decode` and the drain loop are both doing their jobs.

What was actually wrong was the aim, and the first fix for it was wrong too. It
waited for the rectangle to drift somewhere with room to its left.

The rectangle never drifts again by that point in the run. The first arrow press
stops the drift on purpose, which is what lets M9 and M10 assert exact distances
rather than approximate ones, and the steering happens before the drag. So the
wait could only ever time out, on every run, and then aim at wherever the
steering had left it. It passed for as long as it did because that position
usually happened to have room.

Waiting was the wrong idea, and it had been the wrong idea since M9. There is no wait now. The script drags whichever way
has room from where the cursor actually lands, and writes down which way it
chose, so the check asserts what was really asked for instead of a number
written in two places that can disagree. It also aims a quarter of the way into
the rectangle rather than the middle, so the drift takes twice as long to carry
the press off it.

A pointer that reaches an edge anyway is now reported as a run that could not ask
the question rather than as an answer of no. That is told from a real wrong
answer by the movement being short in the direction it was going, which is the
only thing an edge can do to it.

The check that matters did not change: the rectangle has to move exactly as far
as the pointer did, whatever that was.

**Then the rest of the suite was read for the same fault, and one more was
found.** Three checks took the last three captures off the end of a list rather
than naming the ones they were about. That worked only because each happened to
be called straight after its own captures were loaded, so inserting a screenshot
anywhere earlier would have pointed them at the wrong pictures and they would
have gone on passing. They ask for captures by name now, and naming one that was
never taken is an error rather than a shrug.

Everything else in that file holds up. The tiling check looks at every layout
rather than the final one and says why. The focus check refuses to write down
which windows it expects. The rotation check allows the wobble that drawing
explains and points at the host test that measures the real thing. Those were
written to be right rather than to pass.

**A line a script runs is not logged as typed**, which is right, and it means
the boot test cannot look for it in the log. What proves RUN ran is what it left
behind: the script makes a directory at the root, and the machine reports it in
the listing it prints after the restart. Evidence rather than an announcement.

## M28 finishing a name you have started typing

Done. Tab completes a filename against the filesystem.

The shell has directories, paths and files called things like README.TXT, and
until now the only way to reach one was to type all of it correctly. On a
machine whose keyboard is a virtual one inside an emulator, that is not a small
thing.

**The three rules are the ones every shell settled on, and each is a decision.**
One match is finished off, with a slash after it when it is a directory, because
whatever comes next is almost certainly inside it. Several are finished as far
as they all agree and no further, which is the most that can be said without
guessing. Nothing matching leaves the line exactly as it was.

**That last one matters most.** A completion that changed the line when it had
nothing to offer would be worse than one that did nothing, because the line
would then be wrong in a way that looks like something you typed. Taking that
guard out fails a check.

**Finishing one name and finishing several are the same code.** The longest
beginning every match agrees on is built up as the matches are found, and with
one match that beginning is the whole name. Writing them as two cases would have
been two things to keep in step.

**The candidates are shown on the first press, not the second.** One key doing
the whole job is one fewer thing to know, and the list is what tells you which
letter to type next.

**Case does not matter.** Everything this machine draws is upper case, and a
completion that cared would find nothing most of the time.

**Where to look and what to match is worked out once.** Finishing a name and
listing the candidates both need the same three answers, and two copies would be
two chances for them to disagree about which directory they meant.

**The terminal still knows nothing about files.** Tab is handled where the
filesystem is, and the line editor gained one function that replaces the line it
is holding. A terminal that knew about filenames could not be tested without a
filesystem underneath it, which is the same reason the commands were split out
of it in M19.

**The boot test presses a real Tab.** It types `CAT COM`, presses the key, and
runs whatever came out. The kernel logs the command it ran, so if completion had
done nothing the log would say `CAT COM` and the check would fail. It says
`CAT COMPLETED.TXT`.

## M29 a page allocator, so memory stops being decided at compile time

Done. Physical memory is discovered from the memory map the bootloader hands
over, and pages are handed out one at a time.

**Why this, and why now.** Every milestone up to M28 got its memory from an
array whose size was chosen when the kernel was compiled. The window pool is
eight slots because somebody wrote eight. That works until something needs
memory whose size nobody knew in advance, and every part of running a program
is exactly that: an address space, a set of page tables, a stack, and the pages
the program itself is loaded into. So this is the first milestone of the road
towards running real software, and nothing above it can be built without it.

**A bitmap, not a free list.** One bit per four kilobyte page, set when the page
is in use. A free list is the other obvious choice and stores its links inside
the free pages themselves, which means the first thing a stray write does is
corrupt the allocator rather than the caller. The bitmap lives in one place,
can be compared against the memory map at any moment, and costs one bit per
page, which is a thirty-two thousandth of memory. On the 512 MB machine the
tests boot, that is fifteen kilobytes.

**Everything starts in use.** This is the decision the whole safety argument
rests on. `pmm_reset` fills the bitmap with ones, and memory becomes available
only by being explicitly named as usable. That inverts the usual danger.
Forgetting to reserve something does not hand out memory that something else
owns, it merely leaves a page unused. For the kernel's own image, the
framebuffer, or the bootloader's tables to be handed out, the boot code would
have to actively declare them usable, and it never does. There is exactly one
explicit reservation in the whole of boot, for the bitmap itself, because the
bitmap is the one thing that lives inside memory the map did call usable.

**Bootloader reclaimable memory is left alone.** It genuinely is free memory and
a later milestone should take it. It is not taken now because the memory map
itself, the direct map response and the framebuffer description are all sitting
in it, and this kernel reads them after boot. Handing those pages out would
work perfectly right up until something overwrote the structure describing the
screen. Reclaiming it is a real thing to do once everything the bootloader said
has been copied somewhere the kernel owns.

**Page zero is never available, whatever the memory map says.** The allocator
returns a physical address and uses zero to mean it has none, so handing out
physical page zero and failing would be the same answer, and a caller would
treat a real page as an out of memory error. The host test that found this
allocated every page in a small bitmap and counted them: the count came back
one short and the first allocation looked like a failure. The guard is in
`pmm_add_free`, which is the one place every page has to pass through to become
available. No real memory map offers page zero anyway, because it holds the
real mode interrupt vectors and the BIOS data area, but the invariant is not
allowed to depend on that.

**A bad free is refused and counted.** Freeing a page that is already free
would add it to the free count twice, and the same page would then be handed to
two callers. That does not fail where it happens. It fails later, somewhere
unrelated, and looks like a bug in whatever was unlucky enough to be using that
page. So a double free, and a free of an address outside the bitmap, are both
refused and counted in `bad_frees` rather than silently ignored.

**How it is proved.** Two ways, because neither is enough on its own.

The allocator never dereferences a physical address, so the whole of it runs as
an ordinary program on the development machine, where the addresses are made up
and the bitmap is a local array. Forty-eight checks cover the bookkeeping:
partial pages at the ends of a range, the opposite rounding that `reserve` uses
against `add_free`, reserved pages never being handed out, every page being
handed out exactly once and only once, running out reporting empty rather than
wrapping, double frees, out of range frees, a bitmap whose coverage starts
above zero, and a bitmap too small for the page count it was given.

None of that can catch the one thing that only goes wrong on a real machine: a
page the bitmap calls free that cannot actually be written, because the direct
map offset is wrong or the memory map claimed memory that is not there. So the
kernel proves that at boot. It takes eight pages, writes each page's own
physical address into its first and last words, reads them back, checks no two
were the same page, and gives all eight up again. The boot test requires the
line saying it passed, and requires the free page count to be exactly what it
was before.

**What it does not do.** No slab allocator, no buddy system, no allocation of
more than one page at a time, no swapping, no NUMA. Those solve fragmentation
and locality problems ME OS does not have, and a page allocator that is easy to
reason about is worth more right now than a fast one.

## M30 address spaces, so an address stops meaning one thing

Done. The kernel builds x86-64 page table trees, maps and unmaps single pages
in them, reads back what an address currently means, and has run on one it made
itself.

**What was there before.** One universe. Every address meant the same thing to
every part of the machine, because the bootloader's page tables were still in
use and nothing had ever built another set. That is fine while the only code
running is the kernel's own. It becomes impossible the moment two programs are
meant to be unable to read each other, because "unable to read" is not a rule
anybody obeys, it is an address that does not translate.

**The format, briefly.** A 48 bit address is translated through four tables of
512 entries. Nine bits pick an entry at each level and the last twelve are the
offset inside the page. An entry holds the physical address of the next table,
or of the page itself at the last level, with permission bits in the space the
alignment leaves free.

**The split that made this testable.** `paging.c` is the format as arithmetic
and touches no memory. `vmm.c` is the walk and touches page tables, but reaches
every one of them through a direct map offset held in the address space rather
than a constant. That one decision is why the entire walk runs in the host test
suite over an arena of ordinary memory standing in for physical memory: the
addresses are invented, and the offset is the distance between them and the
real buffer. `vmmcpu.c` holds the four things that need a privileged
instruction and has no logic in it at all.

This matters more here than anywhere earlier in the project. A wrong page table
entry does not print anything. It triple faults the virtual machine, usually
somewhere unrelated, and often several milestones after the mistake was made.
Sixty-eight host checks cover mapping, unmapping, translating, offsets inside a
page, refusing to overwrite a live mapping, non canonical addresses, unaligned
addresses, running out of memory partway through building a tree, and tearing
one down.

**Permission is the most restrictive level on the path.** The processor takes a
mapping's permission to be the AND of every entry on the way down, which has a
consequence that is easy to get wrong in the direction of a silent failure: a
user page underneath a table entry with no user bit is simply unreachable from
user mode. So intermediate entries are always present and writable, and gain
the user bit when anything below them is a user page. Widening a parent is safe
because every leaf still carries its own restriction, and there is a test that
maps a kernel page and a user page under the same tables and checks the kernel
one is still not reachable from user mode afterwards. No-execute is never set
on the way down, because forbidding execution on a table entry would forbid it
for every page underneath.

**One kernel, mapped once.** A new address space copies the upper half of the
kernel's top level table, entries 256 to 511. Sharing the top level entries
rather than copying the tables beneath them means there is one kernel, seen
identically from every process, and a change to a kernel mapping does not have
to be repeated into every address space that exists. Those entries never carry
the user bit, so a process can be shown the kernel is there and cannot read it.

**Large pages are refused, not split.** The bootloader maps memory with two
megabyte pages. Walking into one as though it pointed at a table would read the
middle of a mapped page as though it were entries, and then write there.
Splitting one while something is using it changes what an address means
underneath its user. So the walk stops and says so.

**Tearing one down frees tables and never pages.** Only the lower half is
walked, because the upper half belongs to the kernel and is shared. Mapped
pages are left alone: this layer maps a physical page at an address and never
learns whether anybody else mapped the same page somewhere else, so freeing one
would take it from whoever else holds it. A test builds a tree, destroys it,
and checks that the tables came back and the three mapped pages did not.

**How the real machine proves it.** The host tests cannot answer whether the
processor accepts a tree this kernel built, which is a different claim from the
tree looking right. So at boot the kernel builds a second address space, and
before switching checks two things it cannot survive losing: that its current
stack and its own code are both reachable in the new space. Switching to a
space where the stack is not mapped means the next push faults, the fault
handler needs a stack, and the machine triple faults and reboots with nothing
on the screen and nothing in the log. Then it loads the new tree into CR3,
reads a word back through a mapping that exists only in that space, switches
back, and tears the space down. The boot test requires that line, requires
no-execute to be available, and requires the teardown to return page tables.

**What it does not do.** No demand paging, no copy on write, no swapping, no
large page creation, no address space layout randomisation, and no reclaiming
of an intermediate table that has become empty. Each of those is a real thing
to want and none of them is needed to run a program.

## M31 descriptor tables and traps, so being wrong stops a program instead of the machine

Done. The kernel builds and loads its own segment table, has a task state
segment naming a stack to take faults on, fills all 256 interrupt vectors, and
has taken a deliberate fault and carried on.

**Why this has to come before user mode.** A program is supposed to be able to
be wrong. Dividing by zero, reading an address it does not own, or executing
rubbish should stop that program and leave the machine running. Up to M30 there
was no interrupt table at all, which was honest while the only code on the
machine was the kernel's own, because a kernel fault is a bug either way. With
no table the processor cannot find a handler, then cannot report that it cannot
find one, and resets. That is the difference between a broken program and a
silent reboot, and it is the whole milestone.

**Why the bootloader's segment table is not enough.** It is a perfectly good
table and it is replaced anyway, for two things it does not contain. There are
no user segments, and user mode is not a state the kernel asks for: it is what
the processor is in because the code segment selector it is running with says
privilege three. And there is no task state segment, which is what tells the
processor which stack to switch to when a trap arrives from user mode. Without
it the processor would push the trap frame onto the program's own stack, which
is memory the program controls, and the first thing a handler read would be
whatever the program chose to put there.

**Segmentation is nearly gone and the remnant is exactly what is needed.** In
64 bit mode base and limit are ignored. What a code descriptor still decides is
the privilege level, which is the one thing this milestone is about. User data
is placed before user code in the table even though nothing today depends on
the order, because `sysret` derives both selectors from one register and
requires that arrangement, and putting them the other way round would have to
be undone later.

**Reloading the code segment needs a far return.** There is no instruction that
assigns to CS. The only way to change it is a jump carrying a selector, and the
only one left in 64 bit mode is `lretq`, so the selector and a return address
are pushed by hand and a far return is used as a jump to the next line. Until
that happens the new table is loaded and ignored, and the processor is still
running on the bootloader's descriptor. That looks identical from C, which is
why the kernel reads the selector back afterwards and the boot test requires it
to be the kernel code segment of the table this kernel built.

**One stack shape for every vector.** The processor pushes an error code for
some exceptions and not for others, which would make the stack a different
shape depending on what happened. The stubs that do not get one push a zero in
its place, so everything below that point sees one layout. The stubs are
assembly because there is no way to write them in C: a handler is entered with
the processor's frame already on the stack and must leave with every register
as it found it, and a compiler is free to use any register on the way. This is
the first assembly in the project, and the build learned about `.S` files for
it.

**The trap stack is its own array.** Not the boot stack, because the boot stack
is where the faulting code was running, and a fault caused by exhausting that
stack would fault again while pushing the frame, which is a double fault, and
then a triple fault.

**External interrupts stay masked.** Nothing here enables them. The timer, the
keyboard and the mouse are all still polled, so there is nothing to enable them
for, and a masked machine is one fewer thing happening while the rest of this
is being built. Exceptions are not interrupts and arrive regardless, which is
what makes an interrupt table useful before any interrupt is turned on.

**How it is proved.** The bit layouts are their own file and their own test.
A handler address in an interrupt gate is stored in three separate pieces and a
task state segment base in four, and getting one piece wrong does not produce a
wrong answer anybody notices. It produces a jump to an address nobody chose, at
the exact moment something has already gone wrong enough to fault. Forty host
checks write every field and read it back, including addresses that land on and
just past each piece boundary, and check that only the system call gate is
reachable from user mode.

On the real machine the kernel executes `int3` and checks it came back with the
trap counted and the right vector recorded. If the table were wrong that line
would simply never appear, and its absence is the report. The boot test
requires it, requires the code selector to be the kernel's own, and requires
all 256 vectors to be filled, because an unexpected vector with no handler is
the reset this milestone exists to prevent.

**What it does not do.** No external interrupts, no programmable interrupt
controller, no timer interrupt, no preemption, and no recovery from a kernel
fault. A fault in the kernel logs everything it knows, including the faulting
address and the decoded page fault reason, and stops. Stopping leaves the log
and the screen exactly as they were, which is worth more than a reset.

## M32 user mode, so the kernel can run code it does not trust

Done. A program runs at privilege three, in an address space of its own, and
reaches the kernel only through one gate. A program that faults is stopped and
the machine carries on. A program that holds a correct kernel address is
refused anyway.

**Three claims, and the first is the least interesting.** That correct code
runs at privilege three proves the entry path works. That incorrect code stops
there is what makes privilege separation something other than decoration. And
that a program cannot read the kernel is the whole reason the previous three
milestones exist.

The third is worth being precise about. The kernel is mapped in every program's
address space, and has to be: a system call has to land somewhere and the
handler has to exist at the moment the processor arrives. So the pages really
are there, and the test program really does hold a correct address for one. It
is refused because that page has no user bit, and at privilege three that is
the entire difference. The boot check requires the refusal to be at an address
above the kernel's base, because a refusal anywhere else would not prove what
it claims.

**There is no instruction that lowers privilege.** `iretq` is used instead,
because returning from an interrupt restores whatever privilege the saved code
segment names, and nothing checks the processor was ever there. So a frame is
built by hand describing a place the machine has never been, and returned to.

**Coming back is a coroutine switch.** `proc_enter_user` saves the kernel's
callee-saved registers, remembers where its stack was, and drops. `exit`
restores that stack pointer and returns, which returns from `proc_enter_user`
even though nothing ever returned through it. The kernel side of a program is a
function call that has not finished yet.

**Every register is cleared before the drop.** Whatever is left in one is
visible to the program, and at that moment they hold kernel addresses. A
program cannot read the kernel, and being told where it is is still a gift to
anybody trying.

**`int $0x80` rather than the `syscall` instruction.** `syscall` is faster and
is what Linux uses, and it does not switch stacks: it leaves the program's own
stack pointer in place and expects the kernel to find a safe one from a
register the program could also have written. A software interrupt switches to
the stack in the task state segment before running one instruction of kernel
code. The fast path is worth having, and not before the slow one is known to be
correct.

**Every number from a program is hostile until checked.** The kernel runs with
the program's page tables loaded but at privilege zero, where the user bit
stops protecting anything: every mapping in that address space is readable and
writable to it, including the kernel half the program itself cannot touch. So a
handler that followed a user pointer would let any program read or write any
part of the kernel by passing the right number. Every byte crossing the
boundary goes through `uaccess.c`, which checks every page of a range is
present and carries the user bit before touching any of it, refuses a length
that wraps past the top of memory, and copies nothing at all when any part
fails. Forty-nine host checks cover it, including a page mapped in the
program's own space without the user bit, which is what every kernel page looks
like from there.

**The output sink is the shell's own.** A program writes to a `struct cmd_out`,
which is what M25 gave every built in command. So `RUN HELLO > FILE.TXT` and
piping a program's output into another command work through exactly the
machinery that already existed, and none of it had to be taught what a program
is.

**Six calls would have been too many.** There are three: exit, write and
getpid. A call for the time was written and removed before it shipped, because
the only clock this kernel has reports how long since anybody last asked, which
means asking consumes the answer. The desktop asks once a frame to move things
at a rate. A program that could also ask would take ticks the desktop never
sees, and the visible symptom would be the rectangle slowing down while a
program ran. That call needs a counter that accumulates, which needs a timer
interrupt, which is a later milestone.

**The numbers are ME OS's own, not Linux's.** A Linux compatible boundary is a
real goal and it belongs in a translation layer above this one, so that Linux
specific decisions live in a single file. Pretending to be Linux now, badly, in
a handful of scattered places, is the version of that which cannot be undone.

**A program runs with interrupts off, so nothing can preempt one.** Nothing has
enabled interrupts yet, so this costs nothing today, and it is an honest limit:
a program that loops forever currently hangs the machine. Preemption needs a
timer interrupt.

**What it does not do.** No scheduler, no more than one process running at a
time, no fork, no exec, no wait, no signals, no file descriptors, no shared
memory, no threads. The program is embedded in the kernel image, which M33 is
specifically about ending.

## M33 ELF executables, so a program stops being part of the operating system

Done. `/BIN/HELLO` is a 456 byte executable. The bootloader carries it on the
disc as its own file, the kernel copies it into the filesystem, and `RUN
/BIN/HELLO` reads it back, checks it, maps its segments into a new address
space and runs it at privilege three.

**This is the line the whole chain from M29 was walking towards.** Up to M32 a
program could run, and it was still a blob inside the kernel image, which meant
ME OS could run things built into itself. Now it can load, isolate and execute a
program that arrived separately. Everything on the road to running existing
software needs that and nothing else does.

**ELF support is not Linux compatibility, and the difference matters.** A Linux
program in an ELF file expects Linux's system call numbers, Linux's memory
layout and a C library underneath it. What this brings is the container format,
which is the first of four things. Reading one is worth doing on its own,
because it is what lets a file another toolchain produced be run at all.

**A name is a claim and the first four bytes are evidence.** `RUN` has read
files since M27 and everything it could do with one was interpret its lines as
commands. Which kind of file it is now decided by looking at it, which is what
every Unix does. A host test writes a file called HELLO holding text and checks
it is still run as a script, and a file with no suggestive name holding the ELF
magic and checks it is started as a program.

**Every field is a number a stranger wrote.** The reader is its own file and
touches nothing, so all of it runs on the development machine against files
built to be wrong in exactly one way each. Forty-nine checks: a 32 bit file, a
big endian one, one for another processor, a shared object that would need a
dynamic linker, a header table that starts near the end, one whose offset
wraps, more entries than could fit, an entry size that is not ELF64's, a
segment claiming bytes past the end, an offset and length that wrap round
together, a segment holding more than it makes room for, one asking for the
kernel's own address, two wanting the same memory, an entry point outside
everything loaded, and an entry point in a segment that will not be executable.

More memory than file is deliberately allowed, because that is how a program
asks for zeroed space, and the pages are zeroed before anything is copied into
them so it gets what it asked for.

**A segment is not a page.** It has a byte address and a byte length and
neither is necessarily aligned, and two segments may share the page at their
ends. So the loader works out the pages first and then fills them, which is
also the only way the bytes before a segment's start in its first page come out
zero rather than as whatever the last owner left. A page that is already mapped
is filled rather than refused, because the file's own rule is that segments do
not overlap by byte, not that they do not share a page.

**A segment is mapped with the permissions it asked for.** Writable if the
file says so, and not executable unless the file says so, where the processor
can enforce it. Data a program can write and also run is how a mistake in one
becomes control of the other.

**The working program is no longer inside the kernel.** It used to be, at M32,
and keeping a copy there would have undercut exactly the claim this milestone
makes, so it was removed. What remains embedded are the two broken programs,
one that reads a null pointer and one that reads the kernel, and they stay
because they are fixtures rather than programs and nobody would put either on a
disk.

**The filesystem needed a binary write.** `vfs_write` takes a string and finds
its length at the first zero byte, which is right for text and wrong for an
executable. Reading was already safe, because `vfs_read` copies the length the
node records and never looks for a terminator.

**How it is proved.** Three ways. The reader is tested exhaustively on the
development machine. At boot the kernel reads `/BIN/HELLO` back out of the
filesystem, loads it and checks it said the right twenty-one bytes. And the
boot test types `RUN /BIN/HELLO` at the shell like a person would, so the
visible path and the automatic one are both covered. The boot check requires
the installed file to be at least the size of an ELF header, so a truncated or
empty file cannot pass by being unreadable in a way that looks like success.

**What it does not do.** No dynamic linking, no shared libraries, no
relocation, no interpreter, no arguments, no environment, and no more than one
program at a time. `PS` exists and says so honestly: with no scheduler a
program only exists while the shell is inside it, and the shell cannot be
running `PS` at the same moment.

## How a milestone is judged done

1. It runs. Compiling is not passing.
2. `make test` proves it without a human watching, where that is possible.
3. Earlier milestones still work. The M1 message is checked on every run of the
   M2 test for exactly this reason.
4. A person sees it at least once. Automated framebuffer checks catch
   regressions; they do not replace looking.

## Verification status

M1 to M33 are verified in QEMU by automated framebuffer inspection and by the
kernel's own log. None has been observed on physical ME hardware, and physical
machine boot testing is a later step that has not been scheduled.

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
  overflow. Twenty-five host programs run in all now, one per part, and the newest
  of them writes a filesystem to a disk made of memory and then breaks one
  field of it at a time to check that every impossible arrangement is refused.
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

  Since M23 it boots twice. The disk is blanked before the first run, which
  writes a file to it, and the second run is handed the ISO and that disk and
  nothing else. The check fails if the first boot loaded anything, because a
  disk left over from an earlier run would pass without the kernel having
  written a byte. Since M24 it also checks that the largest file spans more
  than one block and that its checksum is the same either side of the restart.
