/* Snake, the whole game, in one userspace program.
 *
 * Arrow keys turn, eating an apple grows the snake, touching a wall or your own
 * body ends the run. Enter plays again and Escape stops.
 *
 * Two things about ME OS shaped how this is built.
 *
 * A program gets one page of stack, so the board and the snake live in statics
 * at file scope. The loader zeroes that memory, which is also the reason a new
 * game only has to clear the part of the board it used.
 *
 * There is no scheduler and no key release event. Nothing else on the machine
 * reads the keyboard while this runs, so the loop has to keep asking, and it
 * cannot ask whether a key is being held. Every turn is a press.
 *
 * See user/README.md for what a program may do.
 */
#include "lib/sys.h"
#include "lib/util.h"

#define CELL      12
#define MAX_COLS  60
#define MAX_ROWS  40
#define MAX_CELLS (MAX_COLS * MAX_ROWS)
#define HUD       22

/* A step is eight short waits rather than one long one. The wait is where all
 * the time goes, and a key pressed during it is not seen until the wait ends,
 * so a single hold of 120 makes the snake feel a tenth of a second late. */
#define SLICE_MS 15
#define SLICES   8

#define C_BG    RGB(14, 18, 24)
#define C_BOARD RGB(20, 26, 34)
#define C_BODY  RGB(90, 230, 140)
#define C_HEAD  RGB(200, 255, 220)
#define C_APPLE RGB(230, 90, 90)
#define C_TEXT  RGB(255, 255, 255)
#define C_DIM   RGB(160, 176, 192)

/* Which cells the snake is standing on, and the order it stood on them.
 *
 * The order is a ring buffer, so growing writes one entry at the head and
 * moving drops one at the tail. Both are a constant amount of work, which
 * matters because the alternative is shuffling the whole body every step.
 *
 * A cell is indexed by row times MAX_COLS plus column, using the maximum and
 * not the board's real width, so the arithmetic does not depend on the window
 * size the desktop happened to give us. */
static unsigned char taken[MAX_CELLS];
static unsigned short order[MAX_CELLS];

static long cols;
static long rows;
static long origin_x;
static long origin_y;
static long win_w;
static long win_h;

static long head_at;
static long tail_at;
static long length;
static long dir_x;
static long dir_y;
static long turned;
static long score;
static long apple;

static void paint_cell(long index, long colour)
{
    const long col = index % MAX_COLS;
    const long row = index / MAX_COLS;
    /* Inset by one so neighbouring segments read as separate squares. */
    win_fill(origin_x + col * CELL + 1, origin_y + row * CELL + 1,
             CELL - 2, CELL - 2, colour);
}

static void clear_cell(long index)
{
    const long col = index % MAX_COLS;
    const long row = index / MAX_COLS;
    win_fill(origin_x + col * CELL, origin_y + row * CELL, CELL, CELL, C_BOARD);
}

static void draw_score(void)
{
    char line[32];
    win_fill(0, 0, win_w, HUD, C_BG);
    win_text(8, 4, u_label(line, "SCORE ", score), C_TEXT, 1);
    if (win_w > 240) {
        win_text(win_w - 116, 4, "ESCAPE QUITS", C_DIM, 1);
    }
}

static void push_head(long index)
{
    taken[index] = 1;
    order[head_at] = (unsigned short)index;
    head_at++;
    if (head_at >= MAX_CELLS) {
        head_at = 0;
    }
    length++;
}

static long pop_tail(void)
{
    const long index = (long)order[tail_at];
    taken[index] = 0;
    tail_at++;
    if (tail_at >= MAX_CELLS) {
        tail_at = 0;
    }
    length--;
    return index;
}

/* An empty cell for the next apple.
 *
 * Random guesses are almost always right and cost nothing. The scan is there
 * for the end of a winning game, when guessing could go round for a very long
 * time before it happens to land on one of the few cells left. */
static long place_apple(void)
{
    for (long tries = 0; tries < 200; tries++) {
        const long index = u_random_under(rows) * MAX_COLS + u_random_under(cols);
        if (taken[index] == 0) {
            return index;
        }
    }
    for (long row = 0; row < rows; row++) {
        for (long col = 0; col < cols; col++) {
            if (taken[row * MAX_COLS + col] == 0) {
                return row * MAX_COLS + col;
            }
        }
    }
    return 0;
}

/* Reads everything waiting. Returns 1 when the player asked to stop.
 *
 * Only the first turn of a step is taken. Two presses between one step and the
 * next could otherwise bend the snake through a right angle and back into its
 * own neck, which looks like the game cheating rather than like a mistake. */
static long read_keys(void)
{
    struct event e;
    while (win_event(&e) == 1) {
        if (e.kind != EV_KEY) {
            continue;
        }
        if (e.key == (unsigned int)KEY_ESCAPE) {
            return 1;
        }
        if (turned != 0) {
            continue;
        }

        long nx = dir_x;
        long ny = dir_y;
        if (e.key == (unsigned int)KEY_UP) {
            nx = 0;
            ny = -1;
        } else if (e.key == (unsigned int)KEY_DOWN) {
            nx = 0;
            ny = 1;
        } else if (e.key == (unsigned int)KEY_LEFT) {
            nx = -1;
            ny = 0;
        } else if (e.key == (unsigned int)KEY_RIGHT) {
            nx = 1;
            ny = 0;
        } else {
            continue;
        }

        /* Turning back the way you came would run the head into the segment
         * behind it. Treat it as a slip of the hand and keep going. */
        if (nx == -dir_x && ny == -dir_y) {
            continue;
        }
        if (nx != dir_x || ny != dir_y) {
            dir_x = nx;
            dir_y = ny;
            turned = 1;
        }
    }
    return 0;
}

static void new_game(void)
{
    u_memset(taken, 0, MAX_CELLS);
    head_at = 0;
    tail_at = 0;
    length = 0;
    score = 0;
    dir_x = 1;
    dir_y = 0;
    turned = 0;

    win_fill(0, 0, win_w, win_h, C_BG);
    win_fill(origin_x, origin_y, cols * CELL, rows * CELL, C_BOARD);

    const long row = rows / 2;
    const long col = cols / 4;
    for (long i = 0; i < 3; i++) {
        const long index = row * MAX_COLS + col + i;
        push_head(index);
        paint_cell(index, i == 2 ? C_HEAD : C_BODY);
    }

    apple = place_apple();
    paint_cell(apple, C_APPLE);
    draw_score();
    win_flush();
}

/* One step. Returns 1 when the snake died. */
static long advance(void)
{
    const long head = (long)order[head_at > 0 ? head_at - 1 : MAX_CELLS - 1];
    const long col = head % MAX_COLS + dir_x;
    const long row = head / MAX_COLS + dir_y;

    if (col < 0 || col >= cols || row < 0 || row >= rows) {
        return 1;
    }
    const long next = row * MAX_COLS + col;
    const long eating = next == apple;

    /* The tail leaves before the head arrives, so following the square that is
     * about to be vacated is legal. It is not legal when eating, because then
     * the tail stays where it is. */
    long freed = -1;
    if (eating == 0) {
        freed = pop_tail();
    }
    if (taken[next] != 0) {
        return 1;
    }

    if (freed >= 0) {
        clear_cell(freed);
    }
    paint_cell(head, C_BODY);
    push_head(next);
    paint_cell(next, C_HEAD);

    if (eating != 0) {
        score++;
        apple = place_apple();
        paint_cell(apple, C_APPLE);
        draw_score();
    }
    win_flush();
    return 0;
}

/* Waits for Enter or Escape. Returns 1 to play again. */
static long wait_choice(void)
{
    for (;;) {
        struct event e;
        while (win_event(&e) == 1) {
            if (e.kind != EV_KEY) {
                continue;
            }
            if (e.key == (unsigned int)KEY_ENTER) {
                return 1;
            }
            if (e.key == (unsigned int)KEY_ESCAPE) {
                return 0;
            }
        }
        hold_ms(20);
    }
}

/* The title screen, which is also where the random numbers come from.
 *
 * getpid is the same on every run, so on its own it would put the first apple
 * in one place forever. How long a person takes to reach for a key is never
 * the same twice, so the two are mixed. Returns 0 if they pressed Escape. */
static long title(void)
{
    win_fill(0, 0, win_w, win_h, C_BG);
    win_text(20, win_h / 2 - 30, "SNAKE", C_BODY, 3);
    win_text(20, win_h / 2 + 6, "ARROW KEYS TURN", C_TEXT, 1);
    win_text(20, win_h / 2 + 22, "ENTER STARTS, ESCAPE QUITS", C_DIM, 1);
    win_flush();

    unsigned long waited = 1;
    for (;;) {
        struct event e;
        while (win_event(&e) == 1) {
            if (e.kind != EV_KEY) {
                continue;
            }
            if (e.key == (unsigned int)KEY_ESCAPE) {
                return 0;
            }
            u_seed((unsigned long)getpid() ^ (waited * 2654435761ul));
            return 1;
        }
        waited++;
        hold_ms(10);
    }
}

static void game_over(void)
{
    char line[32];
    const long y = win_h / 2 - 20;
    win_fill(0, y - 8, win_w, 62, C_BG);
    win_text(20, y, "GAME OVER", C_APPLE, 2);
    win_text(20, y + 24, u_label(line, "SCORE ", score), C_TEXT, 1);
    win_text(20, y + 40, "ENTER PLAYS AGAIN, ESCAPE QUITS", C_DIM, 1);
    win_flush();
}

void _start(void)
{
    const long size = win_open("SNAKE");
    if (size < 0) {
        write("SNAKE COULD NOT OPEN A WINDOW\n");
        exit(1);
    }
    win_w = size >> 32;
    win_h = size & 0xFFFFFFFF;

    /* Laid out from the rectangle the desktop gave, not from a size assumed
     * here, and capped so the board fits the arrays above. */
    cols = u_min((win_w - 8) / CELL, MAX_COLS);
    rows = u_min((win_h - HUD - 8) / CELL, MAX_ROWS);
    if (cols < 10 || rows < 8) {
        write("SNAKE NEEDS A BIGGER WINDOW\n");
        exit(1);
    }
    origin_x = (win_w - cols * CELL) / 2;
    origin_y = HUD + (win_h - HUD - rows * CELL) / 2;

    if (title() == 0) {
        exit(0);
    }

    for (;;) {
        new_game();

        long quit = 0;
        for (;;) {
            turned = 0;
            for (long slice = 0; slice < SLICES && quit == 0; slice++) {
                quit = read_keys();
                hold_ms(SLICE_MS);
            }
            if (quit != 0 || advance() != 0) {
                break;
            }
        }
        if (quit != 0) {
            break;
        }

        game_over();
        if (wait_choice() == 0) {
            break;
        }
    }

    exit(0);
}
