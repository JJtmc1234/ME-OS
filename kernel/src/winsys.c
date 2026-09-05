/* Windows for programs. See kernel/include/winsys.h.
 *
 * Every call here starts the same way: find the window this program owns, and
 * refuse if it has none. A program cannot name another program's window
 * because no call takes a name.
 */
#include "winsys.h"

#include <stddef.h>

#include "log.h"
#include "process.h"
#include "syscall.h"
#include "uaccess.h"

/* How large a piece of text one call may draw. Long enough for a line across
 * any tile this desktop makes, short enough that the copy is bounded. */
#define WINSYS_TEXT_MAX 256

static struct winsys_hooks hooks;

void winsys_attach(const struct winsys_hooks *from)
{
    if (from != NULL) {
        hooks = *from;
    }
}

/* The client area of this program's window, or NULL. */
static struct surface *client_of(struct process *proc)
{
    if (proc == NULL || hooks.desktop == NULL || proc->window_id == 0) {
        return NULL;
    }
    const size_t index = desktop_index_of(hooks.desktop, proc->window_id);
    struct desktop_app *app = desktop_app_at(hooks.desktop, index);
    if (app == NULL) {
        return NULL;
    }
    /* A window on another workspace, or hidden, has a surface with no size.
     * Drawing into it is harmless because the surface layer clips, and it is
     * still worth answering honestly rather than pretending it worked. */
    return &app->client;
}

int64_t winsys_open(struct process *proc, uint64_t title)
{
    if (proc == NULL || hooks.desktop == NULL) {
        return SYS_ENOWINDOW;
    }
    if (proc->window_id != 0) {
        /* One window. Asking twice is a bug in the program rather than a
         * request, so it is refused instead of silently returning the first. */
        return SYS_ENOROOM;
    }

    /* The title is copied into the process, not pointed at in the program's
     * memory. The desktop keeps the pointer for as long as the window exists,
     * and a program that freed or overwrote its own string would otherwise
     * have the taskbar reading whatever replaced it. */
    if (!uaccess_copy_string(&proc->space, proc->window_title,
                             sizeof proc->window_title, title)) {
        return SYS_EFAULT;
    }

    const size_t index = desktop_add(hooks.desktop, proc->window_title);
    if (index >= DESKTOP_MAX_APPS) {
        return SYS_ENOROOM;
    }
    struct desktop_app *app = desktop_app_at(hooks.desktop, index);
    if (app == NULL) {
        return SYS_ENOROOM;
    }
    proc->window_id = app->id;

    /* Every other tile just changed size, and with no scheduler the main loop
     * is stopped inside this program, so nothing else is going to repaint
     * them. */
    if (hooks.relayout != NULL) {
        hooks.relayout();
    }

    const struct surface *client = client_of(proc);
    if (client == NULL) {
        return SYS_ENOWINDOW;
    }
    log_str("winsys: ");
    log_str(proc->name);
    log_str(" opened a window\n");
    log_named_dec("winsys:   width", client->width);
    log_named_dec("winsys:   height", client->height);

    /* Width above, height below, so one register carries both. */
    return (int64_t)(((uint64_t)client->width << 32) | client->height);
}

int64_t winsys_fill(struct process *proc, uint64_t x, uint64_t y,
                    uint64_t width, uint64_t height, uint64_t colour)
{
    struct surface *client = client_of(proc);
    if (client == NULL) {
        return SYS_ENOWINDOW;
    }
    /* A dimension larger than any screen is a mistake rather than a request,
     * and refusing it here means the clip below never sees a number whose
     * conversion would be surprising. */
    if (width > WINDOW_MAX_DIMENSION || height > WINDOW_MAX_DIMENSION) {
        return SYS_EBADSIZE;
    }
    /* Signed on purpose. A program may draw from a negative coordinate and
     * have the visible part appear, which is what any drawing interface does,
     * and the surface layer has clipped both ends since M14. */
    surface_fill_rect(client, (int64_t)(int32_t)x, (int64_t)(int32_t)y,
                      (uint32_t)width, (uint32_t)height, (uint32_t)colour);
    return SYS_OK;
}

int64_t winsys_text(struct process *proc, uint64_t x, uint64_t y,
                    uint64_t text, uint64_t colour, uint64_t scale)
{
    struct surface *client = client_of(proc);
    if (client == NULL) {
        return SYS_ENOWINDOW;
    }
    if (scale == 0 || scale > 8) {
        return SYS_EBADSIZE;
    }

    /* The font is the kernel's. A program drawing its own glyphs would need a
     * font in its own memory, which is a thing to want and not a thing this
     * milestone is about. */
    char line[WINSYS_TEXT_MAX];
    if (!uaccess_copy_string(&proc->space, line, sizeof line, text)) {
        return SYS_EFAULT;
    }
    surface_draw_string(client, line, (int64_t)(int32_t)x, (int64_t)(int32_t)y,
                        (uint32_t)colour, (uint32_t)scale);
    return SYS_OK;
}

int64_t winsys_flush(struct process *proc)
{
    if (client_of(proc) == NULL) {
        return SYS_ENOWINDOW;
    }
    if (hooks.present != NULL) {
        hooks.present();
    }
    return SYS_OK;
}

int64_t winsys_close(struct process *proc)
{
    if (proc == NULL || proc->window_id == 0) {
        return SYS_ENOWINDOW;
    }
    winsys_release(proc);
    return SYS_OK;
}

void winsys_release(struct process *proc)
{
    if (proc == NULL || proc->window_id == 0 || hooks.desktop == NULL) {
        return;
    }
    const uint32_t id = proc->window_id;
    /* Cleared before the removal, so that a relayout which somehow reaches
     * back in here finds a program with no window rather than one pointing at
     * a window that is halfway gone. */
    proc->window_id = 0;
    proc->window_title[0] = '\0';

    if (desktop_remove(hooks.desktop, id) && hooks.relayout != NULL) {
        hooks.relayout();
    }
}

int64_t winsys_event(struct process *proc, uint64_t into)
{
    if (client_of(proc) == NULL) {
        return SYS_ENOWINDOW;
    }
    if (hooks.poll_input == NULL) {
        return 0;
    }

    struct winsys_input raw;
    if (!hooks.poll_input(&raw) || raw.kind == WINSYS_NONE) {
        return 0;
    }

    /* Control and C, which the program is never told about.
     *
     * With no scheduler a program that keeps asking for input keeps the
     * machine, and the runtime limit is five minutes, which is a long time to
     * watch a program you want to stop. This is the way out, and it works
     * because it is the kernel that reads the keyboard on the program's
     * behalf. Delivering it and hoping the program acted on it would be asking
     * a program that will not stop to stop itself. */
    if (raw.stop_requested) {
        process_stop(proc, -3, "somebody pressed control and C");
    }

    struct sys_event out = {
        .kind = raw.kind,
        .key = raw.key,
        .x = 0,
        .y = 0,
        .buttons = raw.buttons,
        .reserved = 0,
    };

    if (raw.kind == WINSYS_POINTER) {
        /* Screen coordinates on the way in, window coordinates on the way out.
         * A program has no idea where its window is and should not be told:
         * given a screen position it would have to be given an origin too, and
         * then a tiling desktop that moved the window under it would be handing
         * out stale numbers. */
        const struct region where =
            desktop_client_region(hooks.desktop, proc->window_id, 0, 0, 1, 1);
        out.x = (int32_t)(raw.screen_x - where.x);
        out.y = (int32_t)(raw.screen_y - where.y);
    }

    if (!uaccess_copy_out(&proc->space, into, &out, sizeof out)) {
        return SYS_EFAULT;
    }
    return 1;
}
