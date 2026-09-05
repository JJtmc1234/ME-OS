/* Tetris for ME OS.
 *
 * The whole executable has to fit in the six kilobyte file limit, so the seven
 * pieces are a table of packed bitmasks rather than lists of coordinates. Each
 * rotation is one sixteen bit word over a four by four grid, bit r * 4 + c, so
 * all seven pieces in all four rotations cost 56 bytes and rotating is a table
 * index instead of arithmetic on every cell.
 *
 * The board lives in a static at file scope. A program gets one page of stack
 * and 200 bytes of board on top of the drawing locals would be uncomfortably
 * close to the end of it.
 *
 * The desktop is tiling, so the cell size is worked out from the window size
 * win_open hands back rather than fixed. There are no key release events, so
 * one press is one move and gravity is counted in frames.
 *
 * See user/README.md.
 */
#include "lib/sys.h"
#include "lib/util.h"

#define COLS 10
#define ROWS 20

/* Seven pieces, four rotations each, bit r * 4 + c of a four by four grid.
 * Pieces with two distinct shapes simply repeat, which keeps one loop instead
 * of a special case per piece. */
static const unsigned short PIECES[7][4] = {
    { 0x00F0, 0x4444, 0x00F0, 0x4444 }, /* I */
    { 0x0071, 0x0226, 0x0470, 0x0322 }, /* J */
    { 0x0074, 0x0622, 0x0170, 0x0223 }, /* L */
    { 0x0066, 0x0066, 0x0066, 0x0066 }, /* O */
    { 0x0036, 0x0462, 0x0036, 0x0462 }, /* S */
    { 0x0072, 0x0262, 0x0270, 0x0232 }, /* T */
    { 0x0063, 0x0264, 0x0063, 0x0264 }, /* Z */
};

static const unsigned int COLOUR[7] = {
    0x30D5C8, 0x3060D0, 0xE08020, 0xE0D040, 0x40C060, 0xA050D0, 0xD04050,
};

/* Points for clearing one to four rows at once. */
static const unsigned short SCORE_FOR[5] = { 0, 100, 300, 500, 800 };

/* Zero is empty. A filled cell holds the piece number plus one, so the colour
 * survives the lock and the board does not need a second array beside it. */
static unsigned char board[ROWS][COLS];

static long win_w, win_h;
static long cell, ox, oy, scale;

static int piece, rot, px, py;
static long score, lines;
static int over;

/* True when piece p in rotation r sits inside the board with every one of its
 * cells on empty ground. Every move is tried against this before it happens,
 * which is also how a landing and a game over are detected. */
static int fits(int p, int r, int x, int y)
{
    for (int i = 0; i < 16; i++) {
        if (((PIECES[p][r] >> i) & 1) == 0) {
            continue;
        }
        const int cx = x + (i & 3);
        const int cy = y + (i >> 2);
        if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
            return 0;
        }
        if (board[cy][cx] != 0) {
            return 0;
        }
    }
    return 1;
}

static void spawn(void)
{
    piece = (int)u_random_under(7);
    rot = 0;
    px = 3;
    py = 0;
    if (!fits(piece, rot, px, py)) {
        over = 1;
    }
}

static void lock_piece(void)
{
    for (int i = 0; i < 16; i++) {
        if (((PIECES[piece][rot] >> i) & 1) != 0) {
            board[py + (i >> 2)][px + (i & 3)] = (unsigned char)(piece + 1);
        }
    }
}

static void clear_lines(void)
{
    int got = 0;

    for (int y = ROWS - 1; y >= 0; y--) {
        int full = 1;
        for (int x = 0; x < COLS; x++) {
            if (board[y][x] == 0) {
                full = 0;
                break;
            }
        }
        if (!full) {
            continue;
        }
        for (int k = y; k > 0; k--) {
            for (int x = 0; x < COLS; x++) {
                board[k][x] = board[k - 1][x];
            }
        }
        for (int x = 0; x < COLS; x++) {
            board[0][x] = 0;
        }
        got++;
        /* A new row has fallen into y and has not been looked at, so undo the
         * step the loop is about to take. */
        y++;
    }

    if (got > 0) {
        lines += got;
        score += SCORE_FOR[got];
    }
}

static void reset(void)
{
    u_memset(board, 0, (long)sizeof board);
    score = 0;
    lines = 0;
    over = 0;
    spawn();
}

/* Cell size from the window the desktop gave us. Six cells of width are held
 * back on the right for the score panel. */
static void layout(void)
{
    const long by_w = (win_w - 8) / (COLS + 6);
    const long by_h = (win_h - 8) / ROWS;

    cell = u_min(by_w, by_h);
    if (cell < 4) {
        cell = 4;
    }
    ox = 4;
    oy = (win_h - cell * ROWS) / 2;
    if (oy < 0) {
        oy = 0;
    }
    scale = u_clamp(cell / 9, 1, 3);
}

static void block(long x, long y, long colour)
{
    /* One pixel short in each direction, which is what draws the grid. */
    win_fill(ox + x * cell, oy + y * cell, cell - 1, cell - 1, colour);
}

static void draw(void)
{
    char line[24];
    const long panel = ox + cell * COLS + cell;

    win_fill(0, 0, win_w, win_h, RGB(10, 12, 18));
    win_fill(ox - 2, oy - 2, cell * COLS + 4, cell * ROWS + 4, RGB(28, 34, 46));

    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            if (board[y][x] != 0) {
                block(x, y, COLOUR[board[y][x] - 1]);
            }
        }
    }

    if (!over) {
        for (int i = 0; i < 16; i++) {
            if (((PIECES[piece][rot] >> i) & 1) != 0) {
                block(px + (i & 3), py + (i >> 2), COLOUR[piece]);
            }
        }
    }

    win_text(panel, oy, "SCORE", RGB(150, 162, 190), scale);
    win_text(panel, oy + 16 * scale, u_itoa(score, line), RGB(255, 255, 255), scale);
    win_text(panel, oy + 44 * scale, "LINES", RGB(150, 162, 190), scale);
    win_text(panel, oy + 60 * scale, u_itoa(lines, line), RGB(255, 255, 255), scale);

    if (over) {
        win_text(ox + 4, oy + cell * ROWS / 2 - 8 * scale, "GAME OVER",
                 RGB(255, 90, 90), scale);
        win_text(ox + 4, oy + cell * ROWS / 2 + 10 * scale, "ENTER AGAIN",
                 RGB(190, 200, 220), scale);
    }
    win_flush();
}

void _start(void)
{
    const long size = win_open("TETRIS");
    if (size < 0) {
        write("COULD NOT OPEN A WINDOW\n");
        exit(1);
    }
    win_w = size >> 32;
    win_h = size & 0xFFFFFFFF;
    layout();

    /* The seed has to come from somewhere the player cannot repeat and getpid
     * is the same every run, so the title screen counts frames until the first
     * key press. That is a person's reaction time and it is never twice the
     * same, which is why the game waits for a key instead of starting. */
    long waited = 0;
    for (;;) {
        struct event e;
        int pressed = 0;
        while (win_event(&e) == 1) {
            if (e.kind == EV_KEY) {
                if ((long)e.key == KEY_ESCAPE) {
                    win_close();
                    exit(0);
                }
                pressed = 1;
            }
        }
        if (pressed) {
            break;
        }
        win_fill(0, 0, win_w, win_h, RGB(10, 12, 18));
        win_text(ox + 8, win_h / 3, "TETRIS", RGB(220, 230, 255),
                 u_clamp(scale + 1, 1, 4));
        win_text(ox + 8, win_h / 3 + 24 * scale, "PRESS ANY KEY",
                 RGB(150, 162, 190), scale);
        win_flush();
        waited++;
        hold_ms(33);
    }
    u_seed((unsigned long)getpid() * 2654435761ul + (unsigned long)waited);
    reset();

    long tick = 0;
    for (;;) {
        struct event e;
        while (win_event(&e) == 1) {
            if (e.kind != EV_KEY) {
                continue;
            }
            const long k = (long)e.key;
            if (k == KEY_ESCAPE) {
                win_close();
                exit(0);
            }
            if (over) {
                if (k == KEY_ENTER) {
                    reset();
                    tick = 0;
                }
                continue;
            }
            if (k == KEY_LEFT) {
                if (fits(piece, rot, px - 1, py)) {
                    px--;
                }
            } else if (k == KEY_RIGHT) {
                if (fits(piece, rot, px + 1, py)) {
                    px++;
                }
            } else if (k == KEY_UP) {
                const int next = (rot + 1) & 3;
                if (fits(piece, next, px, py)) {
                    rot = next;
                }
            } else if (k == KEY_DOWN) {
                if (fits(piece, rot, px, py + 1)) {
                    py++;
                    /* Reset gravity so a held down key falls at the player's
                     * rate and not the player's rate plus the game's. */
                    tick = 0;
                }
            }
        }

        if (!over) {
            /* Frames between gravity steps. Ten cleared lines is a level and
             * each one takes two frames off, down to a floor of three. */
            long step = 20 - (lines / 10) * 2;
            if (step < 3) {
                step = 3;
            }
            if (++tick >= step) {
                tick = 0;
                if (fits(piece, rot, px, py + 1)) {
                    py++;
                } else {
                    lock_piece();
                    clear_lines();
                    spawn();
                }
            }
        }

        draw();
        hold_ms(33);
    }
}
