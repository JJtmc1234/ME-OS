# ME OS architecture

Small on purpose. Each milestone adds one capability, and nothing is built
before a milestone needs it.

## Boot path

```mermaid
flowchart LR
    FW[UEFI firmware, OVMF in QEMU] --> LIM[Limine]
    LIM --> KRN[kernel.elf, ELF64, higher half]
    KRN --> MAIN[kmain]
    MAIN --> FB[framebuffer]
    MAIN --> KBD[PS/2 keyboard]
    MAIN --> LOG[debug port and COM1]
```

1. The firmware starts `EFI/BOOT/BOOTX64.EFI`, which is Limine.
2. Limine reads `boot/limine/limine.conf`, loads `boot/kernel.elf`, sets up long
   mode and a linear framebuffer, and jumps to `kmain`.
3. The kernel checks the Limine base revision and the framebuffer response,
   refuses to continue if either is missing, then draws.

The kernel is linked at `0xffffffff80000000`, the top 2 GiB, which is what
`-mcmodel=kernel` generates code for. `linker.ld` keeps the Limine request
structures in their own segment with `KEEP`, because nothing in the kernel
references them and `--gc-sections` would otherwise discard them.

## Modules

| Module | Responsibility |
| --- | --- |
| `main.c` | milestone logic: what gets drawn, and the input loop |
| `fb.c` | linear framebuffer: clear, fill a rectangle, draw a line, draw a string |
| `font.c` | 5x7 bitmap glyphs for A to Z, 0 to 9 and space |
| `kbd.c` | polled PS/2 keyboard, scancode set 1, with shift |
| `mouse.c` | polled PS/2 mouse: port I/O, packet assembly, and pure decoding |
| `pointer.c` | where the pointer is, and clamping it to the screen |
| `cursor.c` | drawing the cursor, and putting back what it covered |
| `timer.c` | elapsed time, polled from the programmable interval timer |
| `rect.c` | where the moving rectangle is, given how much time has passed |
| `calc.c` | parsing and evaluating a typed sum, conditional or assignment, with checked arithmetic |
| `vars.c` | a fixed table of eight named whole numbers |
| `fpu.c` | turning SSE on, and nothing else: no arithmetic lives here |
| `geometry.c` | sine, cosine, rotation and the turning triangle. The only file with floating point in it |
| `log.c` | diagnostics to QEMU's debug port and to COM1 |
| `mem.c` | memset, memcpy, memmove, memcmp |

`mem.c` exists because GCC emits calls to those four on its own, even with
`-ffreestanding` and even when the source never names them. Without them the
kernel links today by luck and stops linking the moment a future milestone
writes ordinary looking C.

## Decisions worth knowing

**No interrupts yet.** There is no interrupt descriptor table, so an IRQ would
triple fault the machine. Interrupts stay masked, and both the keyboard and the
mouse are polled. The controller's status register says which device a waiting
byte came from, so the two share one port without stealing each other's bytes.
An IDT arrives when a milestone actually needs it.

**Input is split three ways.** `mouse.c` talks to the hardware and assembles
packets, `pointer.c` holds the position and clamps it, `cursor.c` draws it.
A second input device would touch only the first of those, and a different
cursor only the last. Decoding and clamping are pure functions, which is why
both can be tested on an ordinary machine.

**Arithmetic refuses rather than wraps.** Every operation in `calc.c` is
checked before it happens: addition and subtraction against the limits,
multiplication by dividing the limit rather than by multiplying and looking at
the result afterwards, division against zero and against the one case whose
answer will not fit, and powers by repeating a checked multiplication. Checking
afterwards was tried first and did not work: signed overflow is undefined, so
the compiler was entitled to delete the check that was meant to catch it, and it
did. The host tests caught that.

**Variables are stored only once the whole line has parsed.** `calc.c` reads an
assignment, works out the value, and remembers the name. `calc_evaluate` does
the storing, after it has checked that nothing is left over at the end. Storing
as soon as the assignment parsed was tried first, and it meant `X=5 6` stored 5
and then reported an error, which is the worst of both. A line that is refused
now leaves the table exactly as it was.

**Floating point is confined to one file, and the build checks it.** x86-64
guarantees SSE2, so that is what the kernel uses; x87 and AVX are not used at
all. The processor starts with floating point disabled, and there is no
interrupt table to catch a fault, so an SSE instruction executing before CR0 and
CR4 have been set would simply stop the machine. Everything is therefore
compiled with SSE off except `geometry.c`, whose entire public interface takes
and returns integers so that no other file can pass it a double even by mistake.
`make` disassembles every object and refuses to link if an SSE instruction
appears anywhere but `geometry.o`, because a rule nothing enforces is a rule
that quietly stops being true.

`fpu.c` holds the control register work and no arithmetic. `geometry.c` holds
the arithmetic and knows nothing about control registers. The drawing is done
in software, straight into the framebuffer: there is no graphics acceleration
anywhere in this kernel.

**Time comes from a counter, not from the loop.** The PIT's channel 0 counts
down and wraps about every 55 milliseconds. The loop reads it, adds up the
differences, and hands the total to `rect_advance`, so the rectangle moves at
sixty pixels a second on a fast machine and on a slow one. The wrap arithmetic
is a pure function, because getting it wrong makes time jump backwards once per
wrap, which is exactly the kind of fault that hides in a short look at an
emulator. The leftover time between whole pixels is carried, so a thousand small
steps land where one large step would.

**The cursor saves what it covers.** There is no second buffer to draw into, so
the cursor keeps a copy of the pixels underneath and puts them back before it
moves. Anything else that draws has to hide the cursor first, or that copy goes
stale and the cursor smears the old picture back over the new one. That is one
rule to remember, and it is why `draw_key_line` hides and reshows it.

**Framebuffer assumptions are checked, not assumed.** `fb_init` refuses a null
address, a bits per pixel other than 32, a zero dimension, a pitch smaller than
one row, or a pitch that is not a multiple of four. It packs colours from the
framebuffer's own channel masks rather than assuming `0xRRGGBB`. Every pixel
write is clipped.

**Two log sinks.** Port `0xE9` is QEMU's debug console and is inert on real
hardware. COM1 at `0x3F8` is a real serial port, so the same log will be visible
on a physical machine or through `qemu -serial`. Serial writes spin a bounded
number of times, so a missing UART cannot hang the boot.

**Drawing is direct.** No console, no scrolling, no cursor, no input buffer.
`main.c` decides where each line goes and redraws the line it owns.

## Testing without a screen

`make test` runs `scripts/boot-capture.sh`, which boots the ISO with no display,
captures the framebuffer through the QEMU monitor, injects a key press with
`sendkey`, captures again, and quits. `tests/check_boot.py` then reads both
captures as pixel data and asserts:

- every pixel is black or white
- there are exactly two lines of text
- the first line has the right number of glyphs for the M1 message and is
  centred on the screen
- the second line reads `PRESS A KEY` before input and reports the injected key
  afterwards
- both log sinks report a clean boot, a keyboard, and the key that was sent

That is a real check of what the kernel drew, not just that it compiled. It
still does not replace a person watching it boot once.

## What is deliberately absent

No memory manager, no processes, no filesystem, no drivers beyond the keyboard,
no networking, no shell, no agent integration. Each of those arrives when a
milestone requires it, not before.
