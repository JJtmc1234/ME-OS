# Writing a program for ME OS

A program here is an ordinary ELF executable that the bootloader carries on the
disc as its own file. It is not part of the kernel, it runs at privilege three
in an address space of its own, and the only thing it can do to the machine is
make a system call. If it is wrong it stops and the machine carries on.

Everything a program can do is in `user/lib/sys.h`. There is nothing else.

## What is not here

No C library. No `printf`, no `malloc`, no `strlen`, no `memcpy`, no headers
from anywhere. If you want one of those, write it, and write it small.

No floating point. Programs are compiled with the x87 and SSE units switched
off, so `float` and `double` will not compile. Use integers, and fixed point if
you need fractions: keep a value multiplied by 256 and shift right when you use
it.

No startup code. `_start` is the first instruction that runs and there is no
`main`. It must never return, because there is nowhere to return to. Call
`exit()`.

## The rules that will bite

**One page of stack, which is 4096 bytes.** A big array on the stack runs off
the end of it and the program takes a page fault. Anything larger than a few
hundred bytes goes in a `static` at file scope, which lands in memory the loader
zeroes for you.

**The whole executable must be at most 32768 bytes.** That is the largest file
this filesystem holds. Code and writable data use separate page aligned
segments. Check the finished file size rather than assuming it fits.

**Sixteen pages per program, 64 kilobytes in all**, code and data and stack
together.

**A program does not choose its window size.** ME OS is tiling: the desktop
gives you a rectangle and `win_open` returns the one you got. Lay out from that
number. A program that assumes a size draws off the edge, and the edge clips
silently.

## The shape of a program

```c
#include "lib/sys.h"

void _start(void)
{
    const long size = win_open("MY PROGRAM");
    if (size < 0) {
        write("COULD NOT OPEN A WINDOW\n");
        exit(1);
    }
    const long width = size >> 32;
    const long height = size & 0xFFFFFFFF;

    for (;;) {
        struct event e;
        if (win_event(&e) == 1) {
            if (e.kind == EV_KEY && e.key == (unsigned int)KEY_ESCAPE) {
                break;
            }
        }

        win_fill(0, 0, width, height, RGB(14, 18, 24));
        win_text(12, 12, "HELLO", RGB(255, 255, 255), 2);
        win_flush();
        hold_ms(33);
    }
    exit(0);
}
```

## What you can call

| Call | What it does |
| --- | --- |
| `win_open(title)` | Opens the one window. Returns width in the top 32 bits and height in the bottom, or negative on failure. |
| `win_fill(x, y, w, h, colour)` | A filled rectangle in window coordinates. Clipped, so drawing off the edge is safe. |
| `win_text(x, y, text, colour, scale)` | Text in the kernel's font. Scale 1 to 8. A character is 8 by 14 pixels at scale 1. |
| `win_flush()` | Puts what you have drawn on the screen. Nothing appears until you call this. |
| `win_event(&e)` | 1 when it filled `e`, 0 when nothing was waiting. Never blocks. |
| `hold_ms(n)` | Waits about `n` milliseconds. Capped at 5000. |
| `write(text)` | A line into the terminal that started the program. |
| `getpid()` | This program's number. |
| `exit(code)` | Stops. Does not return. |
| `RGB(r, g, b)` | A colour. |

An event has `kind` (`EV_KEY` or `EV_POINTER`), `key`, `x`, `y` and `buttons`.
Pointer coordinates are relative to your window. Bit 0 of `buttons` is the left
button. Keys arrive as uppercase characters, digits and punctuation, or as
`KEY_ESCAPE`, `KEY_ENTER`, `KEY_BACKSPACE`, `KEY_TAB`, `KEY_UP`, `KEY_DOWN`,
`KEY_LEFT`, `KEY_RIGHT`.

There are no key release events. The keyboard decoder reports a key going down
and does not report it coming up, so a game cannot ask whether a key is being
held. Move on each press.

## Time and stopping

There is no scheduler. While your program runs, the machine is running your
program and nothing else, including the desktop and the shell. So:

- `hold_ms` in your loop is what stops a game running as fast as the processor
  can go. Roughly 33 milliseconds is thirty frames a second.
- Nothing else reads the keyboard while you run. If you stop calling
  `win_event`, nobody can type anywhere.
- The kernel checks the five minute limit when a program makes a system call.
  A loop with no calls cannot yet be stopped.
- Somebody can press **control and C** to stop your program. You never see that
  key and cannot refuse it.

Honour Escape as well. It costs two lines and it is what a person will try
first.

## Building one

Nothing in the build has to be edited to check that a program compiles:

```sh
cd ~/Projects/ME/ME-OS
gcc -std=gnu11 -ffreestanding -nostdinc -Os -g -Wall -Wextra -Wshadow \
    -Wconversion -fno-stack-protector -fno-pic -fno-pie -m64 -march=x86-64 \
    -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone \
    -isystem "$(gcc -print-file-name=include)" -Iuser \
    -c user/YOURS.c -o /tmp/yours.o
ld -nostdlib -static -s --build-id=none -Ttext=0x400000 -e _start \
    /tmp/yours.o -o /tmp/yours.elf
ls -l /tmp/yours.elf     # at most 32768 bytes
```

Warnings are errors here in spirit. `-Wconversion` in particular will complain
about implicit narrowing, and it is usually telling you something true.

To put a program on the disc, add its name to `USER_PROGRAMS` in `Makefile`.
Add its module path to `limine.conf`, then run `make iso`.
