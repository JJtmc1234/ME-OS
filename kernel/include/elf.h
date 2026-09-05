/* Reading an ELF64 executable.
 *
 * ELF is the format every Unix has used for executables since the nineties,
 * and understanding it is what lets ME OS run a file somebody else's toolchain
 * produced rather than only bytes this project arranged itself.
 *
 * Being able to read one is not Linux compatibility and must not be mistaken
 * for it. A Linux program in an ELF file expects Linux's system call numbers,
 * Linux's memory layout, and a Linux C library underneath it. What this brings
 * is the first of those four things, which is the format of the container.
 *
 * Nothing here loads anything. It reads a buffer, checks every field against
 * the length of that buffer, and reports what the file says. Mapping the
 * segments is somebody else's job, which is what lets the whole of this be
 * tested on the development machine against files built to be wrong.
 *
 * The rule the whole file follows: a header field is a number a stranger
 * wrote. An offset is not inside the file because it says it is.
 *
 * See M33 in docs/milestones.md.
 */
#ifndef ME_ELF_H
#define ME_ELF_H

#include <stdbool.h>
#include <stdint.h>

/* How many loadable segments one program may have. A compiler typically emits
 * three: code, read only data, and writable data. Eight is room to spare and
 * small enough that the list lives in the structure. */
#define ELF_MAX_SEGMENTS 8

/* The size of the two structures, which the file states and which are checked
 * rather than trusted. */
#define ELF_HEADER_BYTES  64
#define ELF_PHENT_BYTES   56

/* Segment permissions, as ELF spells them. */
#define ELF_X 0x1u
#define ELF_W 0x2u
#define ELF_R 0x4u

enum elf_result {
    ELF_OK = 0,
    ELF_TOO_SMALL,
    ELF_NOT_ELF,
    ELF_NOT_64BIT,
    ELF_NOT_LITTLE_ENDIAN,
    ELF_BAD_VERSION,
    ELF_NOT_EXECUTABLE,
    ELF_WRONG_MACHINE,
    ELF_BAD_HEADER_TABLE,
    ELF_TOO_MANY_SEGMENTS,
    ELF_NO_LOADABLE_SEGMENTS,
    /* A segment claims bytes that are not in the file. */
    ELF_SEGMENT_PAST_END,
    /* A segment wants to live somewhere a program may not. */
    ELF_SEGMENT_OUT_OF_RANGE,
    /* More in memory than in the file is normal, and the other way round is
     * not: it would mean the file holds bytes with nowhere to go. */
    ELF_SEGMENT_SHRINKS,
    /* Two segments want the same page, which cannot be honoured because they
     * may want different permissions on it. */
    ELF_SEGMENTS_OVERLAP,
    /* The first instruction is not inside anything that gets loaded, or is in
     * something that will not be executable. */
    ELF_BAD_ENTRY,
};

struct elf_segment {
    uint64_t vaddr;
    uint64_t offset;
    uint64_t file_bytes;
    uint64_t mem_bytes;
    uint64_t flags;
};

struct elf_image {
    uint64_t entry;
    struct elf_segment segments[ELF_MAX_SEGMENTS];
    uint64_t count;
    /* The lowest and highest addresses any segment covers, so a caller can see
     * how much address space the program wants without walking the list. */
    uint64_t lowest;
    uint64_t highest;
};

/* Reads and checks a file. `lowest` and `highest` bound where a segment is
 * allowed to ask to live, which is policy the loader owns rather than
 * something ELF decides, so it is passed in and this file stays testable
 * without knowing anything about ME OS's memory layout. */
enum elf_result elf_check(const uint8_t *file, uint64_t bytes,
                          uint64_t lowest, uint64_t highest,
                          struct elf_image *out);

/* Whether a buffer even begins like an ELF file. Used to tell a program from a
 * script without reading the whole thing. */
bool elf_looks_like_elf(const uint8_t *file, uint64_t bytes);

const char *elf_result_text(enum elf_result result);

#endif /* ME_ELF_H */
