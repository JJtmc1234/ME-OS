#include "shell.h"

#include "font.h"

/* Gaps inside a bar. Small, because a bar that takes a lot of height to say a
 * little is height the work does not get. */
#define BAR_PAD        6
#define TASK_WIDTH     150
#define TASK_GAP       4
#define LAUNCHER_WIDTH 40

struct theme theme_default(uint32_t (*rgb)(uint8_t, uint8_t, uint8_t))
{
    struct theme theme = {0};
    if (rgb == NULL) {
        return theme;
    }
    /* Dark, and not black. A true black ground makes every border look like a
     * crack in the screen, and there is no contrast left for a window that is
     * meant to look recessed. */
    theme.desktop = rgb(12, 14, 18);
    theme.bar = rgb(20, 23, 29);
    theme.bar_text = rgb(214, 220, 230);
    theme.bar_dim = rgb(112, 122, 138);
    /* One cyan, used for the focused border, the launcher and the ME mark. */
    theme.accent = rgb(72, 214, 224);
    theme.chrome = rgb(28, 32, 40);
    theme.chrome_text = rgb(198, 206, 218);
    theme.window = rgb(17, 20, 25);
    theme.border = rgb(44, 50, 60);
    return theme;
}

static struct tile_area area_make(int64_t x, int64_t y, int64_t w, int64_t h)
{
    if (w <= 0 || h <= 0) {
        const struct tile_area none = {0, 0, 0, 0};
        return none;
    }
    const struct tile_area area = {x, y, w, h};
    return area;
}

struct tile_area shell_client_area(int64_t width, int64_t height,
                                   int64_t border, int64_t title_height)
{
    if (border < 0) border = 0;
    if (title_height < 0) title_height = 0;
    return area_make(border, border + title_height,
                     width - 2 * border,
                     height - 2 * border - title_height);
}

/* An outline `thickness` pixels wide, drawn inside the rectangle rather than
 * around it, so a bordered tile never paints over its neighbour or the gap. */
static void stroke_box(struct surface *surface, int64_t x, int64_t y,
                       int64_t width, int64_t height, int64_t thickness,
                       uint32_t colour)
{
    if (thickness <= 0 || width <= 0 || height <= 0) {
        return;
    }
    if (thickness * 2 > width) thickness = width / 2;
    if (thickness * 2 > height) thickness = height / 2;
    if (thickness <= 0) {
        return;
    }
    surface_fill_rect(surface, x, y, (uint32_t)width, (uint32_t)thickness, colour);
    surface_fill_rect(surface, x, y + height - thickness,
                      (uint32_t)width, (uint32_t)thickness, colour);
    surface_fill_rect(surface, x, y, (uint32_t)thickness, (uint32_t)height, colour);
    surface_fill_rect(surface, x + width - thickness, y,
                      (uint32_t)thickness, (uint32_t)height, colour);
}

bool shell_hide_button(int64_t width, int64_t border, struct tile_area *out)
{
    if (out == NULL) {
        return false;
    }
    const int64_t size = SHELL_TITLE_HEIGHT - 6;
    const int64_t x = width - border - 2 * (size + 4);
    if (size <= 0 || x <= border) {
        *out = area_make(0, 0, 0, 0);
        return false;
    }
    *out = area_make(x, border + 3, size, size);
    return true;
}

bool shell_close_button(int64_t width, int64_t border, struct tile_area *out)
{
    if (out == NULL) {
        return false;
    }
    const int64_t size = SHELL_TITLE_HEIGHT - 6;
    const int64_t x = width - border - (size + 4);
    if (size <= 0 || x <= border) {
        *out = area_make(0, 0, 0, 0);
        return false;
    }
    *out = area_make(x, border + 3, size, size);
    return true;
}

void shell_frame(struct surface *frame, const struct theme *theme,
                 const char *title, bool focused, int64_t border)
{
    if (!surface_valid(frame) || theme == NULL) {
        return;
    }
    const int64_t width = (int64_t)frame->width;
    const int64_t height = (int64_t)frame->height;
    if (border < 0) border = 0;

    surface_fill_rect(frame, 0, 0, frame->width, frame->height, theme->window);

    /* The title strip, only if there is room for it and a client area under it.
     * A tile too short for both keeps the client area, because a window with no
     * content is worth less than one with no title. */
    const struct tile_area client =
        shell_client_area(width, height, border, SHELL_TITLE_HEIGHT);
    if (client.height > 0) {
        surface_fill_rect(frame, border, border,
                          (uint32_t)(width - 2 * border),
                          (uint32_t)SHELL_TITLE_HEIGHT, theme->chrome);

        shell_icon(frame, border + 3, border + 2, SHELL_TITLE_HEIGHT - 4,
                   theme->accent, theme->chrome);

        if (title != NULL) {
            surface_draw_string(frame, title, border + SHELL_TITLE_HEIGHT + 4,
                                border + (SHELL_TITLE_HEIGHT - FONT_HEIGHT) / 2,
                                theme->chrome_text, 1);
        }

        /* Two marks rather than glyphs, so they read at this size: a bar for
         * hide, a cross for close. */
        struct tile_area button;
        if (shell_hide_button(width, border, &button)) {
            surface_fill_rect(frame, button.x, button.y + button.height - 3,
                              (uint32_t)button.width, 2, theme->chrome_text);
        }
        if (shell_close_button(width, border, &button)) {
            surface_draw_line(frame, button.x, button.y,
                              button.x + button.width - 1,
                              button.y + button.height - 1, theme->chrome_text);
            surface_draw_line(frame, button.x + button.width - 1, button.y,
                              button.x, button.y + button.height - 1,
                              theme->chrome_text);
        }
    }

    /* Last, so nothing paints over it. This is the one thing on screen that
     * says which window the keyboard is talking to. */
    shell_focus_border(frame, theme, focused, border);
}

void shell_focus_border(struct surface *frame, const struct theme *theme,
                        bool focused, int64_t border)
{
    if (!surface_valid(frame) || theme == NULL) {
        return;
    }
    stroke_box(frame, 0, 0, (int64_t)frame->width, (int64_t)frame->height,
               border < 0 ? 0 : border,
               focused ? theme->accent : theme->border);
}

/* Writes `value` into `out` as decimal, returning how many characters it used. */
static uint64_t put_number(char *out, uint64_t capacity, uint64_t value)
{
    char digits[20];
    uint64_t n = 0;
    if (value == 0) {
        digits[n++] = '0';
    }
    while (value > 0 && n < sizeof digits) {
        digits[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    uint64_t written = 0;
    while (n > 0 && written + 1 < capacity) {
        out[written++] = digits[--n];
    }
    return written;
}

static uint64_t put_text(char *out, uint64_t capacity, uint64_t at, const char *text)
{
    if (text == NULL) {
        return at;
    }
    for (uint64_t i = 0; text[i] != '\0' && at + 1 < capacity; i++) {
        out[at++] = text[i];
    }
    return at;
}

void shell_top_bar(struct surface *desktop, const struct theme *theme,
                   int64_t width, int64_t height,
                   int64_t workspace, const char *focused,
                   const char *clock, uint64_t uptime_seconds)
{
    if (!surface_valid(desktop) || theme == NULL || height <= 0) {
        return;
    }
    surface_fill_rect(desktop, 0, 0, (uint32_t)width, (uint32_t)height, theme->bar);

    const int64_t text_y = (height - FONT_HEIGHT) / 2;
    shell_icon(desktop, BAR_PAD, (height - SHELL_ICON_SIZE) / 2, SHELL_ICON_SIZE,
               theme->accent, theme->bar);

    int64_t x = BAR_PAD + SHELL_ICON_SIZE + BAR_PAD;
    surface_draw_string(desktop, "ME OS", x, text_y, theme->bar_text, 1);
    x += 6 * FONT_WIDTH + BAR_PAD * 2;

    /* The workspace, then what has focus. The order is deliberate: where you are
     * comes before what you are doing. */
    char line[64];
    uint64_t n = put_text(line, sizeof line, 0, "WS ");
    n += put_number(line + n, sizeof line - n, (uint64_t)(workspace < 0 ? 0 : workspace));
    line[n] = '\0';
    surface_draw_string(desktop, line, x, text_y, theme->accent, 1);
    x += (int64_t)(n + 2) * FONT_WIDTH;

    if (focused != NULL) {
        surface_draw_string(desktop, focused, x, text_y, theme->bar_text, 1);
    }

    /* The time of day on the right when there is one, and the uptime when the
     * clock chip would not answer. Inventing a time would be worse than saying
     * how long the machine has been up, which is true and which this knows. */
    if (clock != NULL && clock[0] != '\0') {
        n = put_text(line, sizeof line, 0, clock);
    } else {
        n = put_text(line, sizeof line, 0, "UP ");
        n += put_number(line + n, sizeof line - n, uptime_seconds / 60);
        n = put_text(line, sizeof line, n, "M ");
        n += put_number(line + n, sizeof line - n, uptime_seconds % 60);
        n = put_text(line, sizeof line, n, "S");
    }
    line[n] = '\0';
    const int64_t right = width - BAR_PAD - (int64_t)n * FONT_WIDTH;
    if (right > x) {
        surface_draw_string(desktop, line, right, text_y, theme->bar_dim, 1);
    }
}

struct tile_area shell_launcher_button(int64_t screen_height, int64_t height)
{
    if (height <= 0) {
        return area_make(0, 0, 0, 0);
    }
    return area_make(BAR_PAD, screen_height - height + 4,
                     LAUNCHER_WIDTH, height - 8);
}

/* Where one task button sits. Kept next to the drawing so a click is matched
 * against the same arithmetic that put the button on the screen. */
static struct tile_area task_button(int64_t screen_height, int64_t height, size_t index)
{
    const int64_t x = BAR_PAD + LAUNCHER_WIDTH + BAR_PAD +
                      (int64_t)index * (TASK_WIDTH + TASK_GAP);
    return area_make(x, screen_height - height + 4, TASK_WIDTH, height - 8);
}

size_t shell_task_at(int64_t screen_width, int64_t screen_height, int64_t height,
                     size_t count, int64_t x, int64_t y)
{
    for (size_t i = 0; i < count; i++) {
        const struct tile_area button = task_button(screen_height, height, i);
        if (button.width <= 0 || button.x + button.width > screen_width) {
            break;
        }
        if (x >= button.x && x < button.x + button.width &&
            y >= button.y && y < button.y + button.height) {
            return i;
        }
    }
    return count;
}

void shell_taskbar(struct surface *desktop, const struct theme *theme,
                   int64_t screen_width, int64_t screen_height, int64_t height,
                   const struct shell_task *tasks, size_t count)
{
    if (!surface_valid(desktop) || theme == NULL || height <= 0) {
        return;
    }
    const int64_t top = screen_height - height;
    surface_fill_rect(desktop, 0, top, (uint32_t)screen_width,
                      (uint32_t)height, theme->bar);

    const struct tile_area launcher = shell_launcher_button(screen_height, height);
    surface_fill_rect(desktop, launcher.x, launcher.y,
                      (uint32_t)launcher.width, (uint32_t)launcher.height,
                      theme->chrome);
    shell_icon(desktop, launcher.x + (launcher.width - SHELL_ICON_SIZE) / 2,
               launcher.y + (launcher.height - SHELL_ICON_SIZE) / 2,
               SHELL_ICON_SIZE, theme->accent, theme->chrome);

    if (tasks == NULL) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        const struct tile_area button = task_button(screen_height, height, i);
        /* Stop rather than wrap or overflow. A taskbar that runs off the screen
         * is a taskbar whose last entries cannot be clicked, and drawing them
         * anyway would say they can. */
        if (button.width <= 0 || button.x + button.width > screen_width) {
            break;
        }
        surface_fill_rect(desktop, button.x, button.y,
                          (uint32_t)button.width, (uint32_t)button.height,
                          theme->chrome);
        if (tasks[i].focused) {
            /* A bar along the top of the button rather than a full border, so
             * the focused tile's own accent border stays the loudest thing. */
            surface_fill_rect(desktop, button.x, button.y,
                              (uint32_t)button.width, 2, theme->accent);
        }
        shell_icon(desktop, button.x + 5,
                   button.y + (button.height - SHELL_ICON_SIZE) / 2,
                   SHELL_ICON_SIZE,
                   tasks[i].hidden ? theme->bar_dim : theme->accent, theme->chrome);
        if (tasks[i].name != NULL) {
            surface_draw_string(desktop, tasks[i].name,
                                button.x + 5 + SHELL_ICON_SIZE + 5,
                                button.y + (button.height - FONT_HEIGHT) / 2,
                                tasks[i].hidden ? theme->bar_dim : theme->bar_text, 1);
        }
    }
}

void shell_icon(struct surface *surface, int64_t x, int64_t y, int64_t size,
                uint32_t accent, uint32_t ground)
{
    if (!surface_valid(surface) || size < 4) {
        return;
    }
    surface_fill_rect(surface, x, y, (uint32_t)size, (uint32_t)size, ground);

    /* Three frames receding inward. Each one is inset further and drawn dimmer,
     * which is what makes it read as depth rather than as three boxes. The
     * corners are left off so the shape is rounded without needing a curve. */
    for (int64_t ring = 0; ring < 3; ring++) {
        const int64_t inset = ring * (size / 6) + 1;
        const int64_t left = x + inset;
        const int64_t top = y + inset;
        const int64_t span = size - 2 * inset;
        if (span < 3) {
            break;
        }
        /* Only the accent is used, so the icon follows whatever the accent is
         * set to rather than needing its own palette. */
        const uint32_t colour = ring == 0 ? accent : accent;
        surface_fill_rect(surface, left + 1, top, (uint32_t)(span - 2), 1, colour);
        surface_fill_rect(surface, left + 1, top + span - 1,
                          (uint32_t)(span - 2), 1, colour);
        surface_fill_rect(surface, left, top + 1, 1, (uint32_t)(span - 2), colour);
        surface_fill_rect(surface, left + span - 1, top + 1, 1,
                          (uint32_t)(span - 2), colour);
    }
}
