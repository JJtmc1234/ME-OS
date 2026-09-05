/* Pong, a game with two paddles and a ball.
 *
 * It is here because a game is the honest test of a window. A still picture
 * proves the kernel can put pixels on a screen. A game proves it can put them
 * there again thirty times a second, take a key press between two of those
 * frames, and never fall behind. Nothing on this machine had asked for that
 * before.
 *
 * Two things about ME OS shape the whole file.
 *
 * There is no floating point. The x87 and SSE units are switched off, so a
 * ball travelling at two and a half pixels a frame cannot be a `float`. Every
 * position and speed below is fixed point: the real value multiplied by 256
 * and kept in a `long`, shifted right by 8 at the moment it becomes a pixel.
 * Integers would round the ball's path into a staircase and the angles would
 * all collapse towards flat.
 *
 * There are no key release events. The keyboard decoder reports a key going
 * down and never reports it coming up, so the program cannot ask whether up is
 * being held. The paddle therefore moves a fixed step on each press and the
 * key repeat of the keyboard itself is what makes a held key look like motion.
 *
 * See user/README.md for what a program may do.
 */
#include "lib/sys.h"
#include "lib/util.h"

#define FX     8            /* a value is the real one times 256 */
#define ONE    (1L << FX)

#define WIN_SCORE  7
#define PADDLE_W   8
#define BALL_W     6
#define MARGIN     14       /* how far a paddle sits in from its wall */
#define BAND       34       /* the score line lives above the court */
#define HUMAN_STEP 16
#define AI_STEP    8        /* under HUMAN_STEP on purpose, see ai() */
#define MAX_VX     (5 * ONE)

#define ST_START 0
#define ST_PLAY  1
#define ST_OVER  2

#define COL_BG    RGB(10, 13, 18)
#define COL_NET   RGB(38, 46, 58)
#define COL_FG    RGB(232, 238, 244)
#define COL_BALL  RGB(255, 208, 72)
#define COL_QUIET RGB(150, 164, 180)

static long width, height;
static long court_top, court_bottom;
static long paddle_h;
static long left_x, right_x;

static long left_y, right_y;        /* paddle tops, in pixels */
static long ball_x, ball_y;         /* fixed point */
static long ball_vx, ball_vy;       /* fixed point, per frame */
static long aim_off;                /* pixels the computer aims wrong by */
static long score_l, score_r;
static char line[24];

/* Puts the ball back in the middle heading at whoever did not concede. */
static void serve(long towards)
{
    ball_x = (width / 2 - BALL_W / 2) << FX;
    ball_y = ((court_top + court_bottom) / 2 - BALL_W / 2) << FX;
    ball_vx = towards * 3 * ONE;
    ball_vy = (u_random_under(2) == 0 ? -1 : 1) * (ONE / 2 + u_random_under(ONE));
    aim_off = u_random_under(paddle_h) - paddle_h / 2;
}

/* A bounce off a paddle, with the angle taken from where along the paddle the
 * ball landed. The ends throw it back steeply and the middle sends it back
 * almost flat, which is the whole reason the game has any skill in it. A
 * paddle that returned every ball at the same angle would give a rally that
 * never changed. */
static void bounce(long paddle_top)
{
    const long centre = (ball_y >> FX) + BALL_W / 2;
    const long offset = centre - (paddle_top + paddle_h / 2);
    const long reach = u_max(1, paddle_h / 2);

    ball_vy = (offset * 3 * ONE) / reach;
    ball_vx = -ball_vx;

    /* Every return is slightly faster, so no rally can go on forever. Capped
     * below MARGIN so the ball can never step clean over a paddle in one frame
     * and come out the far side untouched. */
    ball_vx += ball_vx > 0 ? ONE / 6 : -ONE / 6;
    ball_vx = u_clamp(ball_vx, -MAX_VX, MAX_VX);
}

/* The computer paddle. It is beatable for three separate reasons and it needs
 * all three. It steps slower than a person can, it aims at a point `aim_off`
 * away from the ball rather than at the ball, and it only chases while the
 * ball is coming towards it. A paddle that tracked the exact centre would
 * never lose a point and the game would be a demonstration rather than a
 * game. */
static void ai(void)
{
    const long centre = right_y + paddle_h / 2;
    long target;

    if (ball_vx > 0) {
        target = (ball_y >> FX) + BALL_W / 2 + aim_off;
    } else {
        /* Drifts home while the ball is away rather than waiting where it
         * last hit, so it is not already lined up for the next shot. */
        target = (court_top + court_bottom) / 2;
    }

    if (target > centre + AI_STEP) {
        right_y += AI_STEP;
    } else if (target < centre - AI_STEP) {
        right_y -= AI_STEP;
    }
    right_y = u_clamp(right_y, court_top, court_bottom - paddle_h);
}

/* Overlap between the ball and a paddle standing at `x`. */
static long hits(long x, long paddle_top)
{
    const long bx = ball_x >> FX;
    const long by = ball_y >> FX;

    return bx + BALL_W > x && bx < x + PADDLE_W
        && by + BALL_W > paddle_top && by < paddle_top + paddle_h;
}

/* One frame of ball. Returns 1 when the game has just been won. */
static long step(void)
{
    ball_x += ball_vx;
    ball_y += ball_vy;

    if (ball_y < court_top << FX) {
        ball_y = court_top << FX;
        ball_vy = -ball_vy;
    } else if ((ball_y >> FX) + BALL_W > court_bottom) {
        ball_y = (court_bottom - BALL_W) << FX;
        ball_vy = -ball_vy;
    }

    if (ball_vx < 0 && hits(left_x, left_y)) {
        ball_x = (left_x + PADDLE_W) << FX;
        bounce(left_y);
    } else if (ball_vx > 0 && hits(right_x, right_y)) {
        ball_x = (right_x - BALL_W) << FX;
        bounce(right_y);
    }

    if ((ball_x >> FX) + BALL_W < 0) {
        score_r++;
        serve(-1);
    } else if ((ball_x >> FX) > width) {
        score_l++;
        serve(1);
    }

    ai();
    return score_l >= WIN_SCORE || score_r >= WIN_SCORE;
}

static void centred(long y, const char *text, long colour, long scale)
{
    const long w = slen(text) * 8 * scale;
    win_text(u_max(0, (width - w) / 2), y, text, colour, scale);
}

static void draw(long state)
{
    win_fill(0, 0, width, height, COL_BG);

    /* A dashed net rather than a solid line, because a solid one reads as a
     * wall and players try to bounce off it. */
    for (long y = court_top; y < court_bottom; y += 18) {
        win_fill(width / 2 - 1, y, 2, 9, COL_NET);
    }

    win_text(width / 4, 4, u_itoa(score_l, line), COL_FG, 2);
    win_text(3 * width / 4, 4, u_itoa(score_r, line), COL_FG, 2);

    win_fill(left_x, left_y, PADDLE_W, paddle_h, COL_FG);
    win_fill(right_x, right_y, PADDLE_W, paddle_h, COL_FG);

    if (state == ST_PLAY) {
        win_fill(ball_x >> FX, ball_y >> FX, BALL_W, BALL_W, COL_BALL);
    } else if (state == ST_START) {
        centred(height / 2 - 30, "PONG", COL_FG, 3);
        centred(height / 2 + 12, "ENTER TO PLAY", COL_QUIET, 1);
        centred(height / 2 + 28, "UP AND DOWN TO MOVE", COL_QUIET, 1);
        centred(height / 2 + 44, "ESCAPE TO QUIT", COL_QUIET, 1);
    } else {
        centred(height / 2 - 24, score_l > score_r ? "YOU WIN" : "COMPUTER WINS", COL_FG, 2);
        centred(height / 2 + 12, "ENTER PLAYS AGAIN", COL_QUIET, 1);
    }
}

void _start(void)
{
    const long size = win_open("PONG");
    if (size < 0) {
        write("PONG COULD NOT OPEN A WINDOW\n");
        exit(1);
    }
    width = size >> 32;
    height = size & 0xFFFFFFFF;

    /* Laid out from the size the desktop handed over, because a tiling desktop
     * never gives a program the size it would have chosen. */
    court_top = height > BAND * 3 ? BAND : 0;
    court_bottom = height;
    paddle_h = u_max(12, (court_bottom - court_top) / 5);
    left_x = MARGIN;
    right_x = u_max(left_x + PADDLE_W, width - MARGIN - PADDLE_W);
    left_y = right_y = (court_top + court_bottom) / 2 - paddle_h / 2;

    long state = ST_START;
    long frames = 0;

    for (;;) {
        struct event e;

        /* Every event waiting, not one per frame. At thirty frames a second a
         * key repeat outruns one read per frame and the paddle lags behind the
         * keyboard by a growing pile of unread presses. */
        while (win_event(&e) == 1) {
            if (e.kind != EV_KEY) {
                continue;
            }
            if (e.key == (unsigned int)KEY_ESCAPE) {
                win_close();
                exit(0);
            }
            if (state == ST_PLAY) {
                if (e.key == (unsigned int)KEY_UP) {
                    left_y -= HUMAN_STEP;
                } else if (e.key == (unsigned int)KEY_DOWN) {
                    left_y += HUMAN_STEP;
                }
                left_y = u_clamp(left_y, court_top, court_bottom - paddle_h);
            } else if (e.key == (unsigned int)KEY_ENTER) {
                /* Seeded here and not at the top of the program. `getpid` is
                 * the same every run, so on its own it would serve the same
                 * first ball forever. How many frames went by before somebody
                 * pressed enter is a person's reaction time and is never twice
                 * the same. */
                u_seed((unsigned long)(getpid() * 0x9E3779B9L + frames * 2654435761L));
                score_l = 0;
                score_r = 0;
                left_y = right_y = (court_top + court_bottom) / 2 - paddle_h / 2;
                serve(u_random_under(2) == 0 ? -1 : 1);
                state = ST_PLAY;
            }
        }

        if (state == ST_PLAY && step()) {
            state = ST_OVER;
        }

        draw(state);
        win_flush();
        frames++;

        /* About thirty frames a second. This is also the only thing stopping
         * the game from taking the whole machine, since there is no scheduler
         * and nothing else runs while it does. */
        hold_ms(33);
    }
}
