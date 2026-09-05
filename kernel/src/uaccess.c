/* Crossing the boundary between a program's memory and the kernel's.
 * See kernel/include/uaccess.h for why none of this can be a plain memcpy.
 */
#include "uaccess.h"

#include <stddef.h>

/* One page's worth of a range, starting at `at`, never crossing the boundary
 * into the next page. */
static uint64_t bytes_in_page(uint64_t at, uint64_t remaining)
{
    uint64_t to_edge = PAGE_SIZE - (at & 0xFFFull);
    return (remaining < to_edge) ? remaining : to_edge;
}

/* The kernel address of one user address, or NULL when the page is not the
 * program's to reach. */
static void *user_byte(const struct addrspace *space, uint64_t user, bool need_write)
{
    uint64_t phys = 0;
    uint64_t flags = 0;
    if (vmm_translate(space, user, &phys, &flags) != VMM_OK) {
        return NULL;
    }
    /* The user bit is what says this page belongs to the program rather than
     * to the kernel. Running at privilege zero, the processor would let the
     * kernel read a kernel page here without complaint, which is exactly the
     * hole this check exists to close. */
    if ((flags & PTE_USER) == 0) {
        return NULL;
    }
    if (need_write && (flags & PTE_WRITE) == 0) {
        return NULL;
    }
    return (void *)(uintptr_t)(phys + space->hhdm);
}

bool uaccess_range_ok(const struct addrspace *space, uint64_t user, uint64_t bytes,
                      bool need_write)
{
    if (space == NULL) {
        return false;
    }
    if (bytes == 0) {
        return true;
    }
    /* An address near the top of memory with an enormous length would
     * otherwise wrap and describe a small, harmless looking range. */
    if (user + bytes < user) {
        return false;
    }

    uint64_t at = user & ~0xFFFull;
    uint64_t end = user + bytes;
    while (at < end) {
        if (user_byte(space, at, need_write) == NULL) {
            return false;
        }
        at += PAGE_SIZE;
    }
    return true;
}

bool uaccess_copy_in(const struct addrspace *space, void *dest, uint64_t user,
                     uint64_t bytes)
{
    /* Checked in full before a single byte is copied. A handler acting on half
     * a structure would be acting on a mixture of what the program sent and
     * whatever was in the buffer before. */
    if (!uaccess_range_ok(space, user, bytes, false)) {
        return false;
    }
    uint8_t *out = (uint8_t *)dest;
    uint64_t done = 0;

    while (done < bytes) {
        uint64_t at = user + done;
        uint64_t run = bytes_in_page(at, bytes - done);
        const uint8_t *from = (const uint8_t *)user_byte(space, at, false);
        if (from == NULL) {
            return false;
        }
        for (uint64_t i = 0; i < run; i++) {
            out[done + i] = from[i];
        }
        done += run;
    }
    return true;
}

bool uaccess_copy_out(const struct addrspace *space, uint64_t user, const void *src,
                      uint64_t bytes)
{
    if (!uaccess_range_ok(space, user, bytes, true)) {
        return false;
    }
    const uint8_t *in = (const uint8_t *)src;
    uint64_t done = 0;

    while (done < bytes) {
        uint64_t at = user + done;
        uint64_t run = bytes_in_page(at, bytes - done);
        uint8_t *to = (uint8_t *)user_byte(space, at, true);
        if (to == NULL) {
            return false;
        }
        for (uint64_t i = 0; i < run; i++) {
            to[i] = in[done + i];
        }
        done += run;
    }
    return true;
}

bool uaccess_copy_string(const struct addrspace *space, char *dest, uint64_t capacity,
                         uint64_t user)
{
    if (space == NULL || dest == NULL || capacity == 0) {
        return false;
    }
    /* The length is not known in advance, so the check happens as it goes
     * rather than once at the start. A page is re-checked only when the walk
     * crosses into it, which is what keeps this from being one translation
     * per byte. */
    const uint8_t *page = NULL;
    uint64_t written = 0;

    for (uint64_t i = 0; i < capacity; i++) {
        uint64_t at = user + i;
        if (at < user) {
            return false;  /* wrapped past the top of memory */
        }
        if (page == NULL || (at & 0xFFFull) == 0) {
            page = (const uint8_t *)user_byte(space, at, false);
            if (page == NULL) {
                return false;
            }
            page -= (at & 0xFFFull);
        }
        char c = (char)page[at & 0xFFFull];
        dest[written++] = c;
        if (c == '\0') {
            return true;
        }
    }
    /* Ran out of room before the program's string ended. Refused rather than
     * truncated, because a silently shortened path or message is a bug that
     * shows up somewhere else entirely. */
    dest[capacity - 1] = '\0';
    return false;
}
