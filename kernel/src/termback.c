#include "termback.h"

void termback_keep(struct term *term, const char *row)
{
    if (term == NULL || row == NULL) {
        return;
    }
    for (uint32_t x = 0; x < TERM_MAX_COLS; x++) {
        term->back[term->back_next][x] = x < term->cols ? row[x] : ' ';
    }
    term->back_next = (term->back_next + 1) % TERM_BACK;
    if (term->back_count < TERM_BACK) {
        term->back_count++;
    }

    /* The view stays on the same text rather than on the same line number.
     *
     * Without this, output arriving while somebody is reading the past would
     * pull what they are reading upwards a line at a time, which is the one
     * thing a person looking at scrollback does not want. It stops at the
     * oldest line, because past that the text they were on is gone.
     */
    if (term->back_view > 0 && term->back_view < term->back_count) {
        term->back_view++;
    }
}

uint32_t termback_offset(const struct term *term)
{
    return term == NULL ? 0 : term->back_view;
}

uint32_t termback_held(const struct term *term)
{
    return term == NULL ? 0 : term->back_count;
}

bool termback_to_bottom(struct term *term)
{
    if (term == NULL || term->back_view == 0) {
        return false;
    }
    term->back_view = 0;
    return true;
}

bool termback_scroll(struct term *term, int32_t pages)
{
    if (term == NULL || pages == 0 || term->rows == 0) {
        return false;
    }
    /* A page less one line, so the line you were reading at the edge is still
     * on screen after the jump. A whole page leaves nothing in common between
     * the two views and makes it easy to lose your place. */
    const uint32_t step = term->rows > 1 ? term->rows - 1 : 1;
    const uint32_t was = term->back_view;

    if (pages > 0) {
        const uint64_t want = (uint64_t)was + (uint64_t)step * (uint64_t)pages;
        /* Stopping at the oldest line rather than refusing. Pressing the key
         * again at the top means "as far as you go", not "do nothing". */
        term->back_view = want > term->back_count ? term->back_count
                                                  : (uint32_t)want;
    } else {
        const uint64_t back = (uint64_t)step * (uint64_t)(-pages);
        term->back_view = back >= was ? 0 : was - (uint32_t)back;
    }
    return term->back_view != was;
}

const char *termback_row(const struct term *term, uint32_t line)
{
    if (term == NULL || line >= term->rows) {
        return NULL;
    }
    /* At the bottom, which is where a terminal spends nearly all its life, this
     * is the grid and nothing else happens. Scrollback costs nothing until
     * somebody uses it. */
    if (term->back_view == 0) {
        return term->cells[line];
    }

    /* Above the fold. Screen line `line` counted from the top of the view is
     * `back_view - line` lines above the newest, and anything still within the
     * grid is in the grid rather than in the ring. */
    if (line + 1 > term->back_view) {
        return term->cells[line - term->back_view];
    }

    const uint32_t up = term->back_view - line;   /* how far back this line is */
    if (up > term->back_count) {
        return NULL;   /* older than anything kept */
    }
    /* `back_next` is one past the newest, so counting back from it lands on the
     * line wanted. The addition of TERM_BACK keeps the subtraction positive. */
    const uint32_t at = (term->back_next + TERM_BACK - up) % TERM_BACK;
    return term->back[at];
}
