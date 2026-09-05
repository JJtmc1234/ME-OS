/* Reading an ELF64 executable. See kernel/include/elf.h.
 *
 * Every multi byte field is assembled a byte at a time rather than by casting
 * the buffer to a structure. The file came from somewhere else and nothing
 * promises its fields are aligned, and a structure would also carry whatever
 * padding this compiler chose. The same reason the on disk filesystem format
 * is written out by hand at M23.
 */
#include "elf.h"

#include <stddef.h>

static uint16_t read16(const uint8_t *at)
{
    return (uint16_t)((uint16_t)at[0] | ((uint16_t)at[1] << 8));
}

static uint32_t read32(const uint8_t *at)
{
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) |
           ((uint32_t)at[2] << 16) | ((uint32_t)at[3] << 24);
}

static uint64_t read64(const uint8_t *at)
{
    return (uint64_t)read32(at) | ((uint64_t)read32(at + 4) << 32);
}

bool elf_looks_like_elf(const uint8_t *file, uint64_t bytes)
{
    return file != NULL && bytes >= 4 &&
           file[0] == 0x7F && file[1] == 'E' && file[2] == 'L' && file[3] == 'F';
}

const char *elf_result_text(enum elf_result result)
{
    switch (result) {
    case ELF_OK:                     return "ok";
    case ELF_TOO_SMALL:              return "too small to be an executable";
    case ELF_NOT_ELF:                return "not an executable";
    case ELF_NOT_64BIT:              return "not 64 bit";
    case ELF_NOT_LITTLE_ENDIAN:      return "not little endian";
    case ELF_BAD_VERSION:            return "unknown ELF version";
    case ELF_NOT_EXECUTABLE:         return "not a plain executable";
    case ELF_WRONG_MACHINE:          return "built for a different processor";
    case ELF_BAD_HEADER_TABLE:       return "its header table is not inside the file";
    case ELF_TOO_MANY_SEGMENTS:      return "too many segments";
    case ELF_NO_LOADABLE_SEGMENTS:   return "nothing in it to load";
    case ELF_SEGMENT_PAST_END:       return "a segment claims bytes past the end";
    case ELF_SEGMENT_OUT_OF_RANGE:   return "a segment asks for memory it may not have";
    case ELF_SEGMENT_SHRINKS:        return "a segment holds more than it makes room for";
    case ELF_SEGMENTS_OVERLAP:       return "two segments want the same memory";
    case ELF_BAD_ENTRY:              return "it starts somewhere that is not loaded";
    }
    return "unreadable";
}

/* Whether [start, start + length) fits inside [0, limit) without the sum
 * wrapping. The wrap is the case worth naming: an enormous length makes the
 * end look small and the range look harmless. */
static bool fits(uint64_t start, uint64_t length, uint64_t limit)
{
    if (start > limit) {
        return false;
    }
    if (start + length < start) {
        return false;
    }
    return start + length <= limit;
}

static enum elf_result check_header(const uint8_t *file, uint64_t bytes)
{
    if (bytes < ELF_HEADER_BYTES) {
        return ELF_TOO_SMALL;
    }
    if (!elf_looks_like_elf(file, bytes)) {
        return ELF_NOT_ELF;
    }
    if (file[4] != 2) {
        return ELF_NOT_64BIT;
    }
    if (file[5] != 1) {
        return ELF_NOT_LITTLE_ENDIAN;
    }
    if (file[6] != 1) {
        return ELF_BAD_VERSION;
    }
    /* Type 2 is a plain executable. A shared object is type 3 and needs a
     * dynamic linker, which this kernel does not have, so it is refused rather
     * than loaded to an address it was not built for. */
    if (read16(file + 0x10) != 2) {
        return ELF_NOT_EXECUTABLE;
    }
    if (read16(file + 0x12) != 0x3E) {
        return ELF_WRONG_MACHINE;
    }
    if (read16(file + 0x36) != ELF_PHENT_BYTES) {
        return ELF_BAD_HEADER_TABLE;
    }
    return ELF_OK;
}

enum elf_result elf_check(const uint8_t *file, uint64_t bytes,
                          uint64_t lowest, uint64_t highest,
                          struct elf_image *out)
{
    if (out == NULL) {
        return ELF_NOT_ELF;
    }
    out->count = 0;
    out->entry = 0;
    out->lowest = 0;
    out->highest = 0;

    enum elf_result result = check_header(file, bytes);
    if (result != ELF_OK) {
        return result;
    }

    uint64_t table = read64(file + 0x20);
    uint64_t entries = read16(file + 0x38);
    if (entries == 0) {
        return ELF_NO_LOADABLE_SEGMENTS;
    }
    if (!fits(table, entries * ELF_PHENT_BYTES, bytes)) {
        return ELF_BAD_HEADER_TABLE;
    }

    for (uint64_t i = 0; i < entries; i++) {
        const uint8_t *entry = file + table + i * ELF_PHENT_BYTES;
        /* Type 1 is PT_LOAD. Everything else describes the file rather than
         * asking for memory, and is skipped rather than refused: a stack note
         * or a build identifier is not a reason to reject a program. */
        if (read32(entry) != 1) {
            continue;
        }
        if (out->count >= ELF_MAX_SEGMENTS) {
            return ELF_TOO_MANY_SEGMENTS;
        }

        struct elf_segment segment = {
            .flags      = read32(entry + 0x04),
            .offset     = read64(entry + 0x08),
            .vaddr      = read64(entry + 0x10),
            .file_bytes = read64(entry + 0x20),
            .mem_bytes  = read64(entry + 0x28),
        };

        if (segment.mem_bytes == 0) {
            continue;  /* asks for nothing, so there is nothing to do */
        }
        /* More in memory than in the file is ordinary: it is how a program
         * asks for zeroed space. The other way round would mean bytes in the
         * file with nowhere to go. */
        if (segment.file_bytes > segment.mem_bytes) {
            return ELF_SEGMENT_SHRINKS;
        }
        if (!fits(segment.offset, segment.file_bytes, bytes)) {
            return ELF_SEGMENT_PAST_END;
        }
        if (segment.vaddr + segment.mem_bytes < segment.vaddr) {
            return ELF_SEGMENT_OUT_OF_RANGE;
        }
        if (segment.vaddr < lowest || segment.vaddr + segment.mem_bytes > highest) {
            return ELF_SEGMENT_OUT_OF_RANGE;
        }

        /* Two segments wanting the same page cannot both be honoured, because
         * a page has one set of permissions and they may disagree. */
        for (uint64_t j = 0; j < out->count; j++) {
            const struct elf_segment *other = &out->segments[j];
            uint64_t a_start = segment.vaddr;
            uint64_t a_end = segment.vaddr + segment.mem_bytes;
            uint64_t b_start = other->vaddr;
            uint64_t b_end = other->vaddr + other->mem_bytes;
            if (a_start < b_end && b_start < a_end) {
                return ELF_SEGMENTS_OVERLAP;
            }
        }

        if (out->count == 0 || segment.vaddr < out->lowest) {
            out->lowest = segment.vaddr;
        }
        if (segment.vaddr + segment.mem_bytes > out->highest) {
            out->highest = segment.vaddr + segment.mem_bytes;
        }
        out->segments[out->count++] = segment;
    }

    if (out->count == 0) {
        return ELF_NO_LOADABLE_SEGMENTS;
    }

    /* The first instruction has to be somewhere that will exist and will be
     * allowed to run. A file that starts outside everything it loads jumps
     * into an unmapped address on its first instruction. */
    out->entry = read64(file + 0x18);
    for (uint64_t i = 0; i < out->count; i++) {
        const struct elf_segment *segment = &out->segments[i];
        if (out->entry >= segment->vaddr &&
            out->entry < segment->vaddr + segment->mem_bytes) {
            if ((segment->flags & ELF_X) == 0) {
                return ELF_BAD_ENTRY;
            }
            return ELF_OK;
        }
    }
    return ELF_BAD_ENTRY;
}
