/* Minesweeper for ME OS, played with the pointer.
 *
 * Covered squares hide mines. The left button uncovers one, an empty square
 * opens its empty neighbours along with it, and a number says how many mines
 * touch that square. Uncover everything that is not a mine to win. F puts a
 * flag on the square under the pointer, R deals a new board, Escape stops.
 *
 * This is the first program here that is played with the mouse, which is the
 * reason it was worth writing. EV_POINTER carries a position in window
 * coordinates and bit 0 of buttons is the left button.
 *
 * Three things about ME OS shaped how it is built.
 *
 * A program gets one page of stack, so the board lives in statics at file
 * scope and the flood fill walks an explicit stack in a static array. Filling
 * an open board by recursion would run off the end of the page and fault.
 *
 * There is no button release event and no right button, so a flag goes on with
 * a key rather than a second button, and a click is spotted as the moment bit 0
 * goes from clear to set.
 *
 * getpid is the same on every run, so the seed is mixed with how long the
 * player took to make the first click. Seeded from pid alone, the same board
 * comes up forever.
 *
 * See user/README.md for what a program may do.
 */
#include "lib/sys.h"
#include "lib/util.h"

#define COLS  16
#define ROWS  12
#define CELLS (COLS * ROWS)
#define MINES 28
#define HUD   22

/* What a square is showing. */
#define COVERED 0
#define OPEN    1
#define FLAG    2

#define C_BG    RGB(14, 18, 24)
#define C_BOARD RGB(20, 26, 34)
#define C_COVER RGB(96, 104, 116)
#define C_HOVER RGB(134, 144, 158)
#define C_OPEN  RGB(30, 36, 46)
#define C_MINE  RGB(230, 90, 90)
#define C_FLAG  RGB(250, 190, 60)
#define C_TEXT  RGB(255, 255, 255)
#define C_DIM   RGB(160, 176, 192)

/* The board. At file scope because three arrays of one hundred and ninety two
 * bytes would be half the stack a program is given. */
static unsigned char mined[CELLS];
static unsigned char touching[CELLS];
static unsigned char shown[CELLS];

/* The flood fill's own stack. A square is marked open as it goes on rather
 * than as it comes off, so each one is pushed exactly once and this can never
 * be asked to hold more than the board. */
static unsigned short to_open[CELLS];

static long win_w;
static long win_h;
static long cell;
static long origin_x;
static long origin_y;
static long text_scale;

static long opened;
static long flagged;
static long dealt;
static long over;    /* 0 playing, 1 hit a mine, 2 cleared */
static long hover;   /* square under the pointer, or -1 for none */
static long dirty;
static unsigned int buttons_was;
static unsigned long waited;

/* One colour per count, so a three is told from a one at a glance. Position
 * zero is never drawn and is only there to keep the index honest. */
static const long number_colour[9] = {
    C_OPEN,
    RGB(110, 170, 255),
    RGB(120, 220, 140),
    RGB(240, 130, 130),
    RGB(190, 150, 255),
    RGB(250, 190, 60),
    RGB(90, 220, 220),
    RGB(235, 235, 235),
    RGB(170, 170, 170),
};

static void draw_square(long index)
{
    const long x = origin_x + (index % COLS) * cell;
    const long y = origin_y + (index / COLS) * cell;
    const long state = shown[index];
    long face = C_COVER;

    if (state == OPEN) {
        face = mined[index] != 0 ? C_MINE : C_OPEN;
    } else if (index == hover && over == 0) {
        face = C_HOVER;
    }
    /* Inset by one so the board's colour shows through as a grid. */
    win_fill(x + 1, y + 1, cell - 2, cell - 2, face);

    if (state == FLAG) {
        /* A square of colour rather than a flag shape. The kernel font has no
         * glyph for one, and a block still reads at the smallest cell size a
         * window can give us. */
        win_fill(x + cell / 4, y + cell / 4, cell / 2, cell / 2, C_FLAG);
        return;
    }
    if (state == OPEN && mined[index] == 0 && touching[index] != 0) {
        char digit[2];
        digit[0] = (char)('0' + touching[index]);
        digit[1] = '\0';
        win_text(x + (cell - 8 * text_scale) / 2,
                 y + (cell - 14 * text_scale) / 2,
                 digit, number_colour[touching[index]], text_scale);
    }
}

static void draw(void)
{
    char line[32];

    win_fill(0, 0, win_w, win_h, C_BG);
    win_fill(origin_x, origin_y, COLS * cell, ROWS * cell, C_BOARD);
    for (long i = 0; i < CELLS; i++) {
        draw_square(i);
    }

    win_text(8, 4, u_label(line, "MINES ", MINES - flagged), C_TEXT, 1);
    if (win_w > 280) {
        const char *hint = "F FLAGS   ESCAPE QUITS";
        long colour = C_DIM;
        if (over == 1) {
            hint = "BOOM      R DEALS AGAIN";
            colour = C_MINE;
        } else if (over == 2) {
            hint = "CLEARED   R DEALS AGAIN";
            colour = C_FLAG;
        }
        win_text(win_w - 8 - slen(hint) * 8, 4, hint, colour, 1);
    }
    win_flush();
}

static void deal(long safe)
{
    for (long placed = 0; placed < MINES; ) {
        const long at = u_random_under(CELLS);
        /* The first square a player opens is never a mine. A game that can end
         * on the first click, before anything at all is known, is not a game. */
        if (at == safe || mined[at] != 0) {
            continue;
        }
        mined[at] = 1;
        placed++;
    }

    for (long row = 0; row < ROWS; row++) {
        for (long col = 0; col < COLS; col++) {
            long count = 0;
            for (long dy = -1; dy <= 1; dy++) {
                for (long dx = -1; dx <= 1; dx++) {
                    const long nx = col + dx;
                    const long ny = row + dy;
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) {
                        continue;
                    }
                    count += mined[ny * COLS + nx];
                }
            }
            touching[row * COLS + col] = (unsigned char)count;
        }
    }
}

static void uncover(long start)
{
    long top = 0;

    if (shown[start] != COVERED) {
        return;
    }
    if (mined[start] != 0) {
        over = 1;
        for (long i = 0; i < CELLS; i++) {
            if (mined[i] != 0) {
                shown[i] = OPEN;
            }
        }
        return;
    }

    shown[start] = OPEN;
    opened++;
    to_open[top++] = (unsigned short)start;

    while (top > 0) {
        const long index = (long)to_open[--top];
        /* A square with a number beside it stops the fill. That is the whole
         * rule of the game: the numbers are the edge of what opens for free. */
        if (touching[index] != 0) {
            continue;
        }
        const long col = index % COLS;
        const long row = index / COLS;
        for (long dy = -1; dy <= 1; dy++) {
            for (long dx = -1; dx <= 1; dx++) {
                const long nx = col + dx;
                const long ny = row + dy;
                if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) {
                    continue;
                }
                const long next = ny * COLS + nx;
                /* A flagged square is not COVERED, so the fill leaves it
                 * alone. That is deliberate. A player's flag is a claim about
                 * the board and the game should not overrule it. */
                if (shown[next] != COVERED) {
                    continue;
                }
                shown[next] = OPEN;
                opened++;
                to_open[top++] = (unsigned short)next;
            }
        }
    }

    if (opened == CELLS - MINES) {
        over = 2;
    }
}

static void new_board(void)
{
    u_memset(mined, 0, CELLS);
    u_memset(touching, 0, CELLS);
    u_memset(shown, 0, CELLS);
    opened = 0;
    flagged = 0;
    dealt = 0;
    over = 0;
    dirty = 1;
}

static void set_hover(long px, long py)
{
    long next = -1;

    /* Checked against the origin before dividing, because a coordinate just
     * left of the board divides towards zero and would land on column zero. */
    if (px >= origin_x && py >= origin_y) {
        const long col = (px - origin_x) / cell;
        const long row = (py - origin_y) / cell;
        if (col < COLS && row < ROWS) {
            next = row * COLS + col;
        }
    }
    if (next != hover) {
        hover = next;
        dirty = 1;
    }
}

static void click(void)
{
    if (hover < 0 || over != 0 || shown[hover] == FLAG) {
        return;
    }
    if (dealt == 0) {
        u_seed((unsigned long)getpid() ^ (waited * 2654435761ul));
        deal(hover);
        dealt = 1;
    }
    uncover(hover);
    dirty = 1;
}

static void toggle_flag(void)
{
    if (hover < 0 || over != 0 || shown[hover] == OPEN) {
        return;
    }
    if (shown[hover] == FLAG) {
        shown[hover] = COVERED;
        flagged--;
    } else {
        shown[hover] = FLAG;
        flagged++;
    }
    dirty = 1;
}

void _start(void)
{
    const long size = win_open("MINES");
    if (size < 0) {
        write("MINES COULD NOT OPEN A WINDOW\n");
        exit(1);
    }
    win_w = size >> 32;
    win_h = size & 0xFFFFFFFF;

    /* The board is a fixed sixteen by twelve and the square is whatever that
     * leaves, because the desktop hands out the rectangle and a program that
     * assumes a size draws off the edge. */
    cell = u_min((win_w - 8) / COLS, (win_h - HUD - 8) / ROWS);
    if (cell < 10) {
        write("MINES NEEDS A BIGGER WINDOW\n");
        exit(1);
    }
    /* A digit is eight by fourteen at scale one, so it only doubles once the
     * square has room for it twice over. */
    text_scale = cell >= 30 ? 2 : 1;
    origin_x = (win_w - COLS * cell) / 2;
    origin_y = HUD + (win_h - HUD - ROWS * cell) / 2;

    hover = -1;
    new_board();

    long running = 1;
    while (running != 0) {
        struct event e;
        while (win_event(&e) == 1) {
            if (e.kind == EV_KEY) {
                if (e.key == (unsigned int)KEY_ESCAPE) {
                    running = 0;
                } else if (e.key == 'F') {
                    toggle_flag();
                } else if (e.key == 'R') {
                    new_board();
                }
            } else if (e.kind == EV_POINTER) {
                set_hover(e.x, e.y);
                /* There is no release event, so a click is the frame in which
                 * bit 0 turns on. Without the edge, holding the button down
                 * would open a square under the pointer every frame. */
                if ((e.buttons & 1u) != 0 && (buttons_was & 1u) == 0) {
                    click();
                }
                buttons_was = e.buttons;
            }
        }

        /* Counts only until the board is dealt, which is what makes it a
         * reaction time and not a clock. */
        if (dealt == 0) {
            waited++;
        }
        if (dirty != 0) {
            draw();
            dirty = 0;
        }
        hold_ms(20);
    }

    exit(0);
}
