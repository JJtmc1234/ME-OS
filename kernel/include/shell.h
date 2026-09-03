/* The ME OS Default desktop: the two bars, the frame around a tile, and the
 * icon. Drawing only, into surfaces that somebody else owns.
 *
 * Nothing here knows about the framebuffer, the mouse or the window manager. It
 * is handed a surface and a theme and it paints, which is what lets the whole
 * look be checked on the development machine without booting anything.
 *
 * See M18 in docs/milestones.md.
 */
#ifndef ME_SHELL_H
#define ME_SHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "surface.h"
#include "tile.h"

/* Every colour the desktop uses, in one place.
 *
 * A theme rather than constants scattered through the drawing, because the
 * accent is the first thing a person will want to change and it appears in the
 * focused border, the launcher and the bar at the same time. Those three have to
 * agree, and the only way to be sure they agree is for there to be one of it. */
struct theme {
    uint32_t desktop;      /* the ground the tiles sit on */
    uint32_t bar;          /* both bars */
    uint32_t bar_text;
    uint32_t bar_dim;      /* the parts of a bar that are context, not content */
    uint32_t accent;       /* focused border, launcher, the ME mark */
    uint32_t chrome;       /* a tile's title strip */
    uint32_t chrome_text;
    uint32_t window;       /* inside a tile, before the app draws */
    uint32_t border;       /* an unfocused tile's edge */
};

/* Builds the default dark theme through the caller's colour packer.
 *
 * Taken as a function pointer because packing depends on the framebuffer's
 * channel layout, which is not known until Limine has answered, and because it
 * lets the host tests build a theme with no framebuffer at all. */
struct theme theme_default(uint32_t (*rgb)(uint8_t, uint8_t, uint8_t));

/* How tall a tile's title strip is, and how thick its border. Small on purpose:
 * the focused border is what says which window is active, so the title does not
 * have to shout, and vertical space belongs to the work. */
#define SHELL_TITLE_HEIGHT 18
#define SHELL_ICON_SIZE    16

/* What the app may draw in, inside the frame's border and below its title.
 * Empty if the tile is too small to hold one. */
struct tile_area shell_client_area(int64_t width, int64_t height,
                                   int64_t border, int64_t title_height);

/* Paints one tile's frame into its own surface: the ground, the title strip
 * with its name and its two buttons, and the border.
 *
 * The border is drawn last so it is never painted over, and it is drawn in the
 * accent when focused. That is deliberately the loudest thing on a tile. */
void shell_frame(struct surface *frame, const struct theme *theme,
                 const char *title, bool focused, int64_t border);

/* Repaints only a frame's edge.
 *
 * A focus change moves the accent from one tile to another and changes nothing
 * else, so repainting the whole frame would wipe the app's content in order to
 * recolour four thin strips. This is the only difference between a focused tile
 * and an unfocused one, which is deliberate: the border says which window the
 * keyboard is talking to, and it does not need help from anything else. */
void shell_focus_border(struct surface *frame, const struct theme *theme,
                        bool focused, int64_t border);

/* Where the two title bar buttons sit inside a frame of this width, so a click
 * can be matched against the same rectangles that were drawn. Returns false if
 * the frame is too narrow to hold them. */
bool shell_hide_button(int64_t width, int64_t border, struct tile_area *out);
bool shell_close_button(int64_t width, int64_t border, struct tile_area *out);

/* The top bar: the ME OS mark, every workspace, what has focus, and the clock.
 *
 * `occupied` says which workspaces have a window on them, so the bar shows where
 * your work is rather than only where you are, and `count` is how many there
 * are. `clock` is the time of day, or NULL when the chip would not answer, in
 * which case the bar shows the uptime instead. A bar that invented a time would
 * be worse than one saying how long the machine has been up, which is true. */
void shell_top_bar(struct surface *desktop, const struct theme *theme,
                   int64_t width, int64_t height,
                   int64_t workspace, const bool *occupied, int64_t count,
                   const char *focused,
                   const char *clock, uint64_t uptime_seconds);

/* One entry on the taskbar. */
struct shell_task {
    const char *name;
    bool focused;
    bool hidden;
    /* On a workspace other than the one being looked at. Shown dimmed rather
     * than left out, so a window is never somewhere a person cannot find. */
    bool elsewhere;
};

/* The bottom taskbar: the launcher, then one button per window. */
void shell_taskbar(struct surface *desktop, const struct theme *theme,
                   int64_t screen_width, int64_t screen_height, int64_t height,
                   const struct shell_task *tasks, size_t count);

/* Where the launcher button sits on a taskbar of this height, so a click can be
 * matched against what was drawn. */
struct tile_area shell_launcher_button(int64_t screen_height, int64_t height);

/* Which task button a point falls in, or `count` if none. */
size_t shell_task_at(int64_t screen_width, int64_t screen_height, int64_t height,
                     size_t count, int64_t x, int64_t y);

/* The ME OS mark: nested rounded frames receding inward, each one turned a
 * little further, so it reads as a portal rather than a folder. Drawn from the
 * size rather than from a stored bitmap, so it stays sharp at any size the bar
 * or the taskbar asks for. A placeholder for the real icon, and deliberately
 * simple: this milestone is about the desktop, not the artwork. */
void shell_icon(struct surface *surface, int64_t x, int64_t y, int64_t size,
                uint32_t accent, uint32_t ground);

#endif /* ME_SHELL_H */
