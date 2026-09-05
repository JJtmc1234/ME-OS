/* The ME OS system call interface, for programs written in C.
 *
 * There is no C library. This is not one: it is the thin layer that turns a
 * function call into the one instruction a program is allowed to use to reach
 * the kernel, and nothing else. A real libc is a thing to want and a long way
 * off, and pretending this is the start of one would put the wrong things in
 * it.
 *
 * Deliberately a copy of the numbers in kernel/include/syscall.h rather than
 * an include of it. A program that included a kernel header would be a program
 * compiled against the kernel, which is exactly the coupling the last two
 * milestones were spent removing. If the two ever disagree the tests notice,
 * because every one of them runs the real program against the real kernel.
 *
 * See M34 in docs/milestones.md.
 */
#ifndef ME_USER_SYS_H
#define ME_USER_SYS_H

#define SYS_EXIT      0
#define SYS_WRITE     1
#define SYS_GETPID    2
#define SYS_WIN_OPEN  10
#define SYS_WIN_FILL  11
#define SYS_WIN_TEXT  12
#define SYS_WIN_FLUSH 13
#define SYS_WIN_CLOSE 14
#define SYS_HOLD      15

#define STDOUT 1

/* The fourth argument goes in r10 rather than rcx, where the ordinary C
 * calling convention would put it. The kernel's ABI says so because the
 * `syscall` instruction destroys rcx, and choosing r10 now means the day ME OS
 * moves off `int 0x80` no program has to be rebuilt. The cost is this shuffle,
 * which the compiler does with a register variable. */
static inline long sys6(long n, long a, long b, long c, long d, long e, long f)
{
    long out;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    __asm__ volatile ("int $0x80"
                      : "=a"(out)
                      : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                      : "memory");
    return out;
}

static inline long sys0(long n) { return sys6(n, 0, 0, 0, 0, 0, 0); }
static inline long sys1(long n, long a) { return sys6(n, a, 0, 0, 0, 0, 0); }
static inline long sys3(long n, long a, long b, long c)
{
    return sys6(n, a, b, c, 0, 0, 0);
}
static inline long sys5(long n, long a, long b, long c, long d, long e)
{
    return sys6(n, a, b, c, d, e, 0);
}

/* The length of a string, because there is no library to ask. */
static inline long slen(const char *s)
{
    long n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static inline void exit(int code) { sys1(SYS_EXIT, code); }
static inline long write(const char *text) { return sys3(SYS_WRITE, STDOUT, (long)text, slen(text)); }
static inline long getpid(void) { return sys0(SYS_GETPID); }

/* Opens the program's window. The desktop is tiling, so the size is what it
 * was given rather than what it asked for: width in the top 32 bits, height in
 * the bottom. Negative means it has none. */
static inline long win_open(const char *title) { return sys1(SYS_WIN_OPEN, (long)title); }
static inline long win_fill(long x, long y, long w, long h, long colour)
{
    return sys5(SYS_WIN_FILL, x, y, w, h, colour);
}
static inline long win_text(long x, long y, const char *text, long colour, long scale)
{
    return sys5(SYS_WIN_TEXT, x, y, (long)text, colour, scale);
}
static inline long win_flush(void) { return sys0(SYS_WIN_FLUSH); }
static inline long win_close(void) { return sys0(SYS_WIN_CLOSE); }

/* Waits about this many milliseconds, so a program can be looked at. Not a real
 * wait: nothing else runs while it happens, because there is no scheduler yet.
 * Capped by the kernel at five seconds. */
static inline long hold_ms(long milliseconds) { return sys1(SYS_HOLD, milliseconds); }

#define RGB(r, g, b) (((long)(r) << 16) | ((long)(g) << 8) | (long)(b))

#endif /* ME_USER_SYS_H */
