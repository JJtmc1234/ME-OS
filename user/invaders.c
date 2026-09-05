/* Space Invaders for ME OS.
 *
 * A grid of aliens marches sideways, drops a row when it touches an edge and
 * shoots back. The ship moves on the arrow keys and fires on space. Escape
 * stops. The game ends when the aliens reach the ship's row or the ship has
 * been hit three times.
 *
 * Three things about this machine shape the whole file.
 *
 * There are no key release events, so a program cannot ask whether a key is
 * held. One press is one step and one shot, and the keyboard's own repeat is
 * what makes the ship glide.
 *
 * The stack is one page, so the alien grid and the shots live in static arrays
 * at file scope, where the loader zeroes them for us.
 *
 * A program does not choose its window. Every size below is worked out from
 * the rectangle win_open returned, so there is no pixel count typed in by hand
 * and the game fits whatever the tiling desktop gives it.
 *
 * There is no floating point, so speeds are whole pixels per frame and the
 * fleet's pace is a number of frames between moves rather than a rate.
 */
#include "lib/sys.h"
#include "lib/util.h"

#define COLS   9
#define ROWS   4
#define ALIENS (COLS * ROWS)
#define SHOTS  3
#define BOMBS  5
#define START_LIVES 3

/* A y below zero means the slot is free. Nothing is ever drawn above the head
 * of the window, so one number carries both the position and the emptiness and
 * no second array of flags is needed. */
static unsigned char alive[ALIENS];
static long shot_x[SHOTS];
static long shot_y[SHOTS];
static long bomb_x[BOMBS];
static long bomb_y[BOMBS];

/* The layout, measured once from the window. */
static long width;
static long height;
static long margin;
static long cell_w;
static long cell_h;
static long alien_w;
static long alien_h;
static long field_y;
static long ship_w;
static long ship_h;
static long ship_y;
static long ship_step;
static long shot_step;
static long bomb_step;
static long hud_h;

/* The state of one game. */
static long fleet_x;
static long fleet_y;
static long fleet_dir;
static long ship_x;
static long score;
static long lives;
static long remaining;

static const long ALIEN_COLOUR[ROWS] = {
    RGB(240, 140, 200),
    RGB(140, 120, 250),
    RGB(90, 230, 140),
    RGB(250, 190, 60),
};

static void measure(void)
{
    hud_h = 18;

    cell_w = u_max(width / (COLS + 2), 4);
    alien_w = u_max(cell_w * 2 / 3, 2);

    ship_h = u_max(height / 24, 4);
    ship_w = u_max(cell_w, 8);
    ship_y = height - ship_h - 4;

    /* The fleet block takes four of these and the rest is the room it has to
     * descend through, so a taller window means a longer game and not a bigger
     * alien. */
    cell_h = u_max((ship_y - hud_h) / (ROWS + 6), 4);
    alien_h = u_max(cell_h / 2, 2);
    field_y = hud_h + cell_h;

    margin = cell_w / 2;
    ship_step = u_max(cell_w / 2, 3);
    shot_step = u_max(height / 50, 3);
    bomb_step = u_max(height / 90, 2);
}

static long alien_left(long index) { return fleet_x + (index % COLS) * cell_w; }
static long alien_top(long index) { return field_y + fleet_y + (index / COLS) * cell_h; }

static long overlaps(long ax, long ay, long aw, long ah,
                     long bx, long by, long bw, long bh)
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static void new_game(void)
{
    for (long i = 0; i < ALIENS; i++) {
        alive[i] = 1;
    }
    for (long i = 0; i < SHOTS; i++) {
        shot_y[i] = -1;
    }
    for (long i = 0; i < BOMBS; i++) {
        bomb_y[i] = -1;
    }
    remaining = ALIENS;
    score = 0;
    lives = START_LIVES;
    fleet_y = 0;
    fleet_dir = 1;
    /* Centred on the window rather than at a fixed left edge, because the last
     * alien in a row is narrower than its cell. */
    fleet_x = (width - ((COLS - 1) * cell_w + alien_w)) / 2;
    ship_x = (width - ship_w) / 2;
}

/* A fresh grid with the score and the lives kept, for when a wave is cleared. */
static void next_wave(void)
{
    for (long i = 0; i < ALIENS; i++) {
        alive[i] = 1;
    }
    for (long i = 0; i < BOMBS; i++) {
        bomb_y[i] = -1;
    }
    remaining = ALIENS;
    fleet_y = 0;
    fleet_dir = 1;
    fleet_x = (width - ((COLS - 1) * cell_w + alien_w)) / 2;
}

static void fire_shot(void)
{
    for (long i = 0; i < SHOTS; i++) {
        if (shot_y[i] < 0) {
            shot_x[i] = ship_x + ship_w / 2 - 1;
            shot_y[i] = ship_y - shot_step;
            return;
        }
    }
}

/* Only the lowest alien in a column may drop a bomb, so a bomb never falls out
 * of the middle of the fleet and through its own front row. */
static void alien_fire(void)
{
    long pick;
    long seen = -1;
    long column = 0;
    long lowest = -1;

    if (remaining <= 0 || u_random_under(100) >= 24) {
        return;
    }

    pick = u_random_under(remaining);
    for (long i = 0; i < ALIENS; i++) {
        if (alive[i] != 0) {
            seen++;
            if (seen == pick) {
                column = i % COLS;
                break;
            }
        }
    }
    for (long row = 0; row < ROWS; row++) {
        if (alive[row * COLS + column] != 0) {
            lowest = row * COLS + column;
        }
    }
    if (lowest < 0) {
        return;
    }
    for (long i = 0; i < BOMBS; i++) {
        if (bomb_y[i] < 0) {
            bomb_x[i] = alien_left(lowest) + alien_w / 2 - 1;
            bomb_y[i] = alien_top(lowest) + alien_h;
            return;
        }
    }
}

static void step_fleet(void)
{
    long min_x = width;
    long max_x = 0;
    long step;

    for (long i = 0; i < ALIENS; i++) {
        if (alive[i] == 0) {
            continue;
        }
        min_x = u_min(min_x, alien_left(i));
        max_x = u_max(max_x, alien_left(i) + alien_w);
    }
    if (remaining <= 0) {
        return;
    }

    step = fleet_dir * u_max(cell_w / 4, 1);
    if (min_x + step < margin || max_x + step > width - margin) {
        fleet_dir = -fleet_dir;
        fleet_y += u_max(cell_h / 2, 2);
    } else {
        fleet_x += step;
    }
    alien_fire();
}

/* Moves every shot and bomb and settles what they hit. Returns 1 while the
 * game is still going and 0 once it is over. */
static long step_projectiles(void)
{
    for (long s = 0; s < SHOTS; s++) {
        if (shot_y[s] < 0) {
            continue;
        }
        shot_y[s] -= shot_step;
        if (shot_y[s] < hud_h) {
            shot_y[s] = -1;
            continue;
        }
        for (long i = 0; i < ALIENS; i++) {
            if (alive[i] == 0) {
                continue;
            }
            if (overlaps(shot_x[s], shot_y[s], 2, shot_step,
                         alien_left(i), alien_top(i), alien_w, alien_h)) {
                alive[i] = 0;
                remaining--;
                /* The back rows are worth more, which is the one reward for
                 * shooting past the aliens that are about to land. */
                score += 10 * (ROWS - i / COLS);
                shot_y[s] = -1;
                break;
            }
        }
    }

    for (long b = 0; b < BOMBS; b++) {
        if (bomb_y[b] < 0) {
            continue;
        }
        bomb_y[b] += bomb_step;
        if (bomb_y[b] > height) {
            bomb_y[b] = -1;
            continue;
        }
        if (overlaps(bomb_x[b], bomb_y[b], 2, bomb_step,
                     ship_x, ship_y, ship_w, ship_h)) {
            bomb_y[b] = -1;
            lives--;
            /* Clearing the sky on a hit stops the second bomb of a pair taking
             * a life the player had no frame to answer. */
            for (long i = 0; i < BOMBS; i++) {
                bomb_y[i] = -1;
            }
            if (lives <= 0) {
                return 0;
            }
        }
    }

    for (long i = 0; i < ALIENS; i++) {
        if (alive[i] != 0 && alien_top(i) + alien_h >= ship_y) {
            return 0;
        }
    }
    if (remaining <= 0) {
        next_wave();
    }
    return 1;
}

static void draw(long playing)
{
    char line[40];

    win_fill(0, 0, width, height, RGB(10, 12, 18));

    u_label(line, "SCORE ", score);
    win_text(6, 2, line, RGB(255, 255, 255), 1);
    u_label(line, "LIVES ", lives);
    win_text(width - 8 * 8 - 6, 2, line, RGB(160, 176, 192), 1);
    win_fill(0, hud_h, width, 1, RGB(40, 50, 64));

    for (long i = 0; i < ALIENS; i++) {
        if (alive[i] != 0) {
            win_fill(alien_left(i), alien_top(i), alien_w, alien_h,
                     ALIEN_COLOUR[i / COLS]);
        }
    }

    for (long s = 0; s < SHOTS; s++) {
        if (shot_y[s] >= 0) {
            win_fill(shot_x[s], shot_y[s], 2, shot_step, RGB(255, 255, 200));
        }
    }
    for (long b = 0; b < BOMBS; b++) {
        if (bomb_y[b] >= 0) {
            win_fill(bomb_x[b], bomb_y[b], 2, bomb_step, RGB(255, 90, 70));
        }
    }

    if (playing != 0) {
        win_fill(ship_x, ship_y, ship_w, ship_h, RGB(90, 220, 255));
        /* A stub of a barrel, so it is obvious which way the shot leaves. */
        win_fill(ship_x + ship_w / 2 - 1, ship_y - ship_h / 2, 3, ship_h / 2,
                 RGB(90, 220, 255));
    } else {
        const long scale = width >= 240 ? 2 : 1;
        const long y = height / 2 - 7 * scale;
        win_text(width / 2 - 9 * 8 * scale / 2, y, "GAME OVER",
                 RGB(255, 120, 120), scale);
        win_text(width / 2 - 22 * 8 / 2, y + 16 * scale,
                 "ENTER PLAYS AGAIN, ESCAPE QUITS", RGB(160, 176, 192), 1);
    }
    win_flush();
}

void _start(void)
{
    const long size = win_open("INVADERS");
    long playing = 1;
    long frame = 0;
    long seeded = 0;

    if (size < 0) {
        write("INVADERS COULD NOT OPEN A WINDOW\n");
        exit(1);
    }
    width = size >> 32;
    height = size & 0xFFFFFFFF;

    measure();
    new_game();
    u_seed((unsigned long)getpid());

    for (;;) {
        struct event e;
        long pace;

        /* Every event waiting, not one per frame. A held key arrives as a run
         * of repeats and taking them all is what turns the repeat into smooth
         * movement instead of a queue that grows behind the game. */
        while (win_event(&e) == 1) {
            if (e.kind != EV_KEY) {
                continue;
            }
            if (e.key == (unsigned int)KEY_ESCAPE) {
                write("INVADERS: STOPPED\n");
                exit(0);
            }
            if (seeded == 0) {
                /* The pid is the same every run. How long the player took to
                 * touch a key is not, so the first press is what decides which
                 * aliens shoot. */
                u_seed((unsigned long)(getpid() * 2654435761L + frame));
                seeded = 1;
            }
            if (playing == 0) {
                if (e.key == (unsigned int)KEY_ENTER) {
                    new_game();
                    playing = 1;
                }
                continue;
            }
            if (e.key == (unsigned int)KEY_LEFT) {
                ship_x = u_max(ship_x - ship_step, margin);
            } else if (e.key == (unsigned int)KEY_RIGHT) {
                ship_x = u_min(ship_x + ship_step, width - margin - ship_w);
            } else if (e.key == ' ') {
                fire_shot();
            }
        }

        if (playing != 0) {
            /* The fleet moves once every so many frames, and the count falls
             * as the grid empties. That is the whole difficulty curve, and it
             * costs one division. */
            pace = 1 + remaining * 6 / ALIENS;
            if (frame % pace == 0) {
                step_fleet();
            }
            playing = step_projectiles();
        }

        draw(playing);
        frame++;
        hold_ms(33);
    }
}
