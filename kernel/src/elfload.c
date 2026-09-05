/* Turning a checked ELF file into a running process.
 *
 * elf.c reads the file and decides whether it is one. This maps what it found,
 * and the split is deliberate: everything that can be got wrong by trusting a
 * number in a file is over there, tested against files built to be wrong,
 * and what is left here is arithmetic on values that have already been
 * checked.
 *
 * A segment is not a page. It has a byte address and a byte length and neither
 * is necessarily aligned, and two segments may share a page at their ends. So
 * the pages are worked out first, then filled, which is also the only way the
 * memory a segment asks for but does not supply comes out zero.
 *
 * See M33 in docs/milestones.md.
 */
#include "elfload.h"

#include <stddef.h>

#include "log.h"
#include "mem.h"

/* Where a program may live. Below the kernel, above the first page so a null
 * pointer faults, and small enough that a runaway segment length is refused
 * rather than eating the machine's memory. */
#define USER_LOWEST  0x0000000000010000ull
#define USER_HIGHEST 0x0000000010000000ull

/* Page table flags for what the segment header asked for. */
static uint64_t flags_for(uint64_t elf_flags)
{
    uint64_t flags = 0;
    if ((elf_flags & ELF_W) != 0) {
        flags |= PTE_WRITE;
    }
    /* Not executable unless the file says so, and only where the processor can
     * enforce it. Data a program can write and also run is how a mistake in
     * one becomes control of the other. */
    if ((elf_flags & ELF_X) == 0 && vmm_nx_available()) {
        flags |= PTE_NX;
    }
    return flags;
}

enum elf_result elfload(struct process *proc, const uint8_t *file, uint64_t bytes)
{
    struct elf_image image;
    enum elf_result result = elf_check(file, bytes, USER_LOWEST, USER_HIGHEST, &image);
    if (result != ELF_OK) {
        return result;
    }

    for (uint64_t i = 0; i < image.count; i++) {
        const struct elf_segment *segment = &image.segments[i];
        uint64_t flags = flags_for(segment->flags);

        /* From the page the segment starts in to the page its last byte is in.
         * A segment rarely begins on a page boundary, and the bytes before it
         * in that page belong to nobody, which is why the page is zeroed. */
        uint64_t first = segment->vaddr & ~0xFFFull;
        uint64_t last = (segment->vaddr + segment->mem_bytes - 1) & ~0xFFFull;

        for (uint64_t at = first; at <= last; at += PAGE_SIZE) {
            /* Which bytes of the file, if any, land in this page. */
            const uint8_t *from = NULL;
            uint64_t take = 0;
            uint64_t into = 0;

            uint64_t page_end = at + PAGE_SIZE;
            uint64_t data_start = segment->vaddr;
            uint64_t data_end = segment->vaddr + segment->file_bytes;

            if (data_start < page_end && data_end > at) {
                uint64_t start = (data_start > at) ? data_start : at;
                uint64_t end = (data_end < page_end) ? data_end : page_end;
                into = start - at;
                take = end - start;
                from = file + segment->offset + (start - segment->vaddr);
            }

            if (!process_add_page_at(proc, at, flags, from, take, into)) {
                return ELF_SEGMENT_OUT_OF_RANGE;
            }
        }
    }

    proc->entry = image.entry;
    log_named_hex("elfload: entry", image.entry);
    log_named_dec("elfload: segments", image.count);
    return ELF_OK;
}
