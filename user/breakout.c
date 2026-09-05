/* Breakout for ME OS.
 *
 * A userspace program, so the whole of it is opening the one window the
 * desktop hands out, drawing rectangles and text into it, and reading events.
 * The shape of the code comes from three limits in user/README.md.
 *
 * There is no floating point, so the ball keeps its position and its speed
 * multiplied by 256 and shifts down to whole pixels only when it is drawn.
 * A ball that moved in whole pixels could only travel at angles the integers
 * allow, which is a very short list.
 *
 * The stack is one page, so the brick grid and the rest of the game state are
 * statics at file scope, where the loader zeroes them.
 *
 * The desktop is tiling and picks the window size, so every measurement below
 * is worked out from what win_open returned. Nothing here assumes a size.
 *
 * There are no key release events, so the paddle moves one step per press and
 * leans on the keyboard repeat rate. The pointer is accepted as well, because
 * a mouse gives the smooth control that a repeating key cannot.
 */
#include "lib/sys.h"
#include "lib/util.h"

#define MAX_COLS 16
#define MAX_ROWS 8

/* One pixel is this many units of the fixed point the ball moves in. */
#define FIX 256

#define ST_WAIT 0
#define ST_PLAY 1
#define ST_OVER 2
#define ST_WIN  3

static unsigned char bricks[MAX_ROWS][MAX_COLS];

static long win_w, win_h;
static long hud_h, hud_scale;
static long cols, rows, cell_w, cell_h, grid_x, grid_y;
static long pad_w, pad_h, pad_x, pad_y, pad_step;
static long ball_x, ball_y, ball_vx, ball_vy, ball_r;
static long score, lives, remaining, total, state;

static const long row_colour[MAX_ROWS] = {
    RGB(232, 74, 74),   RGB(232, 138, 60),  RGB(226, 200, 62),
    RGB(96, 200, 96),   RGB(70, 168, 226),  RGB(132, 120, 224),
    RGB(206, 96, 200),  RGB(150, 160, 176),
};

/* Everything the game measures, from the size the desktop gave us. */
static void layout(void)
{
    hud_scale = win_w >= 480 ? 2 : 1;
    hud_h = 14 * hud_scale + 6;

    cols = u_clamp((win_w - 8) / 48, 4, MAX_COLS);
    cell_w = (win_w - 8) / cols;
    grid_x = (win_w - cols * cell_w) / 2;

    cell_h = u_clamp(win_h / 26, 8, 18);
    /* Bricks get the top third at most, so there is room to play under them. */
    rows = u_clamp((win_h / 3) / cell_h, 2, MAX_ROWS);
    grid_y = hud_h + 6;

    pad_h = u_clamp(win_h / 60, 4, 8);
    pad_w = u_clamp(win_w / 7, 24, 140);
    pad_y = win_h - pad_h - 8;
    pad_x = (win_w - pad_w) / 2;
    pad_step = u_max(10, pad_w / 3);

    ball_r = u_clamp(win_w / 140, 3, 6);
    total = rows * cols;
}

static void reset_bricks(void)
{
    u_memset(bricks, 0, (long)sizeof bricks);
    for (long r = 0; r < rows; r++) {
        for (long c = 0; c < cols; c++) {
            bricks[r][c] = 1;
        }
    }
    remaining = total;
}

/* The ball sits on the paddle until it is launched, so it rides along with it. */
static void park_ball(void)
{
    ball_x = (pad_x + pad_w / 2) * FIX;
    ball_y = (pad_y - ball_r - 1) * FIX;
    ball_vx = 0;
    ball_vy = 0;
}

/* Speeds up as the wall comes down, which is what makes the last bricks hard. */
static long ball_speed(void)
{
    return u_clamp(576 + (total - remaining) * 8, 576, 1280);
}

static void launch(void)
{
    const long speed = ball_speed();
    ball_vy = -speed;
    ball_vx = u_random_under(2) == 0 ? -speed / 2 : speed / 2;
    state = ST_PLAY;
}

static void new_game(void)
{
    score = 0;
    lives = 3;
    reset_bricks();
    pad_x = (win_w - pad_w) / 2;
    park_ball();
    state = ST_WAIT;
}

/* Where the ball lands on the paddle sets the angle it leaves at, so the
 * player steers with the paddle instead of only blocking with it. */
static void bounce_off_paddle(void)
{
    const long speed = ball_speed();
    const long half = u_max(1, pad_w / 2);
    const long offset = ball_x / FIX - (pad_x + half);

    ball_vy = -speed;
    ball_vx = u_clamp((offset * speed) / half, -speed * 3 / 2, speed * 3 / 2);
}

static void hit_brick(long r, long c, long was_x)
{
    const long cell_left = grid_x + c * cell_w;

    bricks[r][c] = 0;
    remaining--;
    score += 10 * (rows - r);

    /* Which way to bounce depends on which way the ball came in. If it was
     * already inside this column before it moved, it arrived from above or
     * below, so the vertical speed is the one to turn round. */
    if (was_x >= cell_left && was_x < cell_left + cell_w) {
        ball_vy = -ball_vy;
    } else {
        ball_vx = -ball_vx;
    }

    if (remaining == 0) {
        state = ST_WIN;
    }
}

/* One frame of movement, cut into four so the ball cannot step straight over
 * a brick or through the paddle when it is moving quickly. */
static void advance(void)
{
    for (long s = 0; s < 4; s++) {
        const long was_x = ball_x / FIX;

        ball_x += ball_vx / 4;
        ball_y += ball_vy / 4;

        const long cx = ball_x / FIX;
        const long cy = ball_y / FIX;

        if (cx - ball_r < 0) {
            ball_x = ball_r * FIX;
            ball_vx = -ball_vx;
        } else if (cx + ball_r > win_w) {
            ball_x = (win_w - ball_r) * FIX;
            ball_vx = -ball_vx;
        }

        /* The top wall is the bottom of the score line, so the ball never
         * covers it. */
        if (cy - ball_r < hud_h) {
            ball_y = (hud_h + ball_r) * FIX;
            ball_vy = -ball_vy;
        }

        const long gc = (cx - grid_x) / cell_w;
        const long gr = (cy - grid_y) / cell_h;
        if (cx >= grid_x && cy >= grid_y && gc < cols && gr < rows && bricks[gr][gc]) {
            hit_brick(gr, gc, was_x);
            if (state == ST_WIN) {
                return;
            }
            continue;
        }

        if (ball_vy > 0 && cy + ball_r >= pad_y && cy - ball_r <= pad_y + pad_h
            && cx + ball_r >= pad_x && cx - ball_r <= pad_x + pad_w) {
            ball_y = (pad_y - ball_r) * FIX;
            bounce_off_paddle();
            continue;
        }

        if (cy - ball_r > win_h) {
            lives--;
            if (lives <= 0) {
                state = ST_OVER;
            } else {
                park_ball();
                state = ST_WAIT;
            }
            return;
        }
    }
}

/* Clamped to zero because win_fill is documented as clipped and win_text is
 * not, so a long message on a narrow window stays inside the window. */
static void centred(long y, const char *text, long colour, long scale)
{
    win_text(u_max(0, (win_w - slen(text) * 8 * scale) / 2), y, text, colour, scale);
}

static void draw(void)
{
    char line[40];

    win_fill(0, 0, win_w, win_h, RGB(10, 12, 18));

    win_text(4, 3, u_label(line, "SCORE ", score), RGB(220, 226, 236), hud_scale);
    u_label(line, "LIVES ", lives);
    win_text(u_max(0, win_w - 4 - slen(line) * 8 * hud_scale), 3, line,
             RGB(220, 226, 236), hud_scale);

    for (long r = 0; r < rows; r++) {
        for (long c = 0; c < cols; c++) {
            if (bricks[r][c]) {
                win_fill(grid_x + c * cell_w + 1, grid_y + r * cell_h + 1,
                         cell_w - 2, cell_h - 2, row_colour[r]);
            }
        }
    }

    win_fill(pad_x, pad_y, pad_w, pad_h, RGB(226, 230, 240));
    win_fill(ball_x / FIX - ball_r, ball_y / FIX - ball_r,
             ball_r * 2, ball_r * 2, RGB(255, 240, 130));

    if (state == ST_WAIT) {
        centred(pad_y - 40, "PRESS ENTER", RGB(150, 160, 180), hud_scale);
    } else if (state == ST_WIN) {
        centred(win_h / 2 - 20, "CLEARED", RGB(120, 230, 140), hud_scale + 1);
        centred(win_h / 2 + 10, "ENTER TO PLAY AGAIN", RGB(150, 160, 180), hud_scale);
    } else if (state == ST_OVER) {
        centred(win_h / 2 - 20, "GAME OVER", RGB(232, 90, 90), hud_scale + 1);
        centred(win_h / 2 + 10, "ENTER TO PLAY AGAIN", RGB(150, 160, 180), hud_scale);
    }

    win_flush();
}

void _start(void)
{
    const long size = win_open("BREAKOUT");
    if (size < 0) {
        write("COULD NOT OPEN A WINDOW\n");
        exit(1);
    }
    win_w = size >> 32;
    win_h = size & 0xFFFFFFFF;

    layout();
    new_game();

    /* Counts frames spent waiting for the first launch. A person's reaction
     * time is never the same twice, so it is the one number here the player
     * cannot repeat, and mixing it into the seed stops every run starting the
     * ball off in the same direction. */
    long waited = 0;

    for (;;) {
        struct event e;
        while (win_event(&e) == 1) {
            if (e.kind == EV_KEY) {
                if (e.key == (unsigned int)KEY_ESCAPE) {
                    win_close();
                    exit(0);
                }
                if (e.key == (unsigned int)KEY_LEFT) {
                    pad_x = u_max(0, pad_x - pad_step);
                } else if (e.key == (unsigned int)KEY_RIGHT) {
                    pad_x = u_min(win_w - pad_w, pad_x + pad_step);
                } else if (e.key == (unsigned int)KEY_ENTER || e.key == ' ') {
                    if (state == ST_WAIT) {
                        u_seed((unsigned long)(getpid() * 2654435761l + waited));
                        launch();
                    } else if (state == ST_OVER || state == ST_WIN) {
                        new_game();
                    }
                }
            } else if (e.kind == EV_POINTER) {
                pad_x = u_clamp(e.x - pad_w / 2, 0, win_w - pad_w);
            }
        }

        if (state == ST_WAIT) {
            waited++;
            park_ball();
        } else if (state == ST_PLAY) {
            advance();
        }

        draw();
        /* Nothing else runs while this program does, so this hold is the whole
         * of the frame rate and the only thing giving the machine back. */
        hold_ms(33);
    }
}
