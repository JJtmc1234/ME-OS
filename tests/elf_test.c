/* Host tests for the M33 ELF reader.
 *
 * Every field in an executable is a number somebody else wrote, and the file
 * may have been built by a toolchain this project has never seen or by
 * somebody trying to get in. So the tests that matter are the malformed ones,
 * and the shape of nearly all of them is the same: a length or an offset that
 * points outside the file it came in.
 *
 * The file here is built field by field rather than by compiling something, so
 * a test can make one wrong in exactly one way and leave everything else
 * correct. A real linker will not produce most of these.
 */
#include <stdio.h>
#include <string.h>

#include "elf.h"

static int failures;

static void check(int condition, const char *what)
{
    if (condition) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s\n", what);
        failures++;
    }
}

/* Where a program is allowed to live, matching what the loader passes. */
#define LOW  0x0000000000400000ull
#define HIGH 0x0000000000800000ull

#define FILE_BYTES 1024
/* One page, so a test can put a segment below where a program may live. */
#define PAGE_ALIGNED_STEP 0x1000ull
static uint8_t file[FILE_BYTES];

static void put16(uint64_t at, uint16_t value)
{
    file[at] = (uint8_t)(value & 0xFF);
    file[at + 1] = (uint8_t)(value >> 8);
}

static void put32(uint64_t at, uint32_t value)
{
    for (int i = 0; i < 4; i++) {
        file[at + (uint64_t)i] = (uint8_t)((value >> (i * 8)) & 0xFF);
    }
}

static void put64(uint64_t at, uint64_t value)
{
    for (int i = 0; i < 8; i++) {
        file[at + (uint64_t)i] = (uint8_t)((value >> (i * 8)) & 0xFF);
    }
}

/* One correct executable: a header, one loadable segment of code at LOW, and
 * the entry point at the start of it. Every test starts from this and breaks
 * exactly one thing. */
#define PHOFF 64
#define CODE_AT 128
#define CODE_BYTES 32

static void build_good(void)
{
    memset(file, 0, sizeof file);
    file[0] = 0x7F; file[1] = 'E'; file[2] = 'L'; file[3] = 'F';
    file[4] = 2;    /* 64 bit */
    file[5] = 1;    /* little endian */
    file[6] = 1;    /* version */
    put16(0x10, 2);         /* a plain executable */
    put16(0x12, 0x3E);      /* x86-64 */
    put32(0x14, 1);
    put64(0x18, LOW);       /* entry */
    put64(0x20, PHOFF);     /* program headers here */
    put16(0x34, 64);        /* header size */
    put16(0x36, 56);        /* one program header entry is 56 bytes */
    put16(0x38, 1);         /* one of them */

    put32(PHOFF + 0x00, 1);           /* PT_LOAD */
    put32(PHOFF + 0x04, ELF_R | ELF_X);
    put64(PHOFF + 0x08, CODE_AT);     /* offset in the file */
    put64(PHOFF + 0x10, LOW);         /* where it goes */
    put64(PHOFF + 0x20, CODE_BYTES);  /* bytes in the file */
    put64(PHOFF + 0x28, CODE_BYTES);  /* bytes in memory */
}

static enum elf_result run(void)
{
    struct elf_image image;
    return elf_check(file, sizeof file, LOW, HIGH, &image);
}

int main(void)
{
    struct elf_image image;

    printf("a correct executable is read and its segment reported\n");
    build_good();
    check(elf_check(file, sizeof file, LOW, HIGH, &image) == ELF_OK, "it is accepted");
    check(image.count == 1, "with one loadable segment");
    check(image.entry == LOW, "the entry point is where the header said");
    check(image.segments[0].vaddr == LOW, "the segment goes where it asked");
    check(image.segments[0].file_bytes == CODE_BYTES, "with the right length");
    check((image.segments[0].flags & ELF_X) != 0, "and it is executable");
    check(image.lowest == LOW, "the lowest address is recorded");
    check(image.highest == LOW + CODE_BYTES, "and so is the highest");

    printf("something that is not an executable is refused\n");
    build_good();
    file[1] = 'X';
    check(run() == ELF_NOT_ELF, "a wrong magic number");
    check(!elf_looks_like_elf(file, sizeof file), "and it does not look like one either");
    build_good();
    check(elf_looks_like_elf(file, sizeof file), "a real one does look like one");
    check(!elf_looks_like_elf(file, 3), "three bytes are not enough to tell");
    check(!elf_looks_like_elf(NULL, 64), "and nothing at all is not one");
    check(elf_check(file, 8, LOW, HIGH, &image) == ELF_TOO_SMALL,
          "a file shorter than a header is refused before anything is read");

    printf("an executable for a different machine is refused\n");
    build_good();
    file[4] = 1;
    check(run() == ELF_NOT_64BIT, "a 32 bit file");
    build_good();
    file[5] = 2;
    check(run() == ELF_NOT_LITTLE_ENDIAN, "a big endian file");
    build_good();
    file[6] = 9;
    check(run() == ELF_BAD_VERSION, "an ELF version nobody has heard of");
    build_good();
    put16(0x12, 0xB7);
    check(run() == ELF_WRONG_MACHINE, "one built for a different processor");
    build_good();
    put16(0x10, 3);
    check(run() == ELF_NOT_EXECUTABLE,
          "a shared object, which would need a dynamic linker");

    printf("a header table that is not inside the file is refused\n");
    build_good();
    put64(0x20, FILE_BYTES - 8);
    check(run() == ELF_BAD_HEADER_TABLE, "a table starting near the end");
    build_good();
    put64(0x20, 0xFFFFFFFFFFFFFF00ull);
    check(run() == ELF_BAD_HEADER_TABLE, "a table offset that would wrap");
    build_good();
    put16(0x38, 4000);
    check(run() == ELF_BAD_HEADER_TABLE, "more entries than could fit");
    build_good();
    put16(0x36, 32);
    check(run() == ELF_BAD_HEADER_TABLE, "an entry size that is not ELF64's");
    build_good();
    put16(0x38, 0);
    check(run() == ELF_NO_LOADABLE_SEGMENTS, "no program headers at all");

    printf("a segment claiming bytes the file does not have is refused\n");
    build_good();
    put64(PHOFF + 0x20, FILE_BYTES);
    put64(PHOFF + 0x28, FILE_BYTES);
    check(run() == ELF_SEGMENT_PAST_END, "a length running past the end");
    build_good();
    put64(PHOFF + 0x08, FILE_BYTES + 1);
    check(run() == ELF_SEGMENT_PAST_END, "an offset past the end");
    build_good();
    put64(PHOFF + 0x08, 0xFFFFFFFFFFFFFFF0ull);
    put64(PHOFF + 0x20, 0x100);
    put64(PHOFF + 0x28, 0x100);
    check(run() == ELF_SEGMENT_PAST_END, "an offset and length that wrap round");

    printf("a segment holding more than it makes room for is refused\n");
    build_good();
    put64(PHOFF + 0x20, 64);
    put64(PHOFF + 0x28, 32);
    check(run() == ELF_SEGMENT_SHRINKS, "more bytes in the file than in memory");

    printf("more memory than file is ordinary, because that is how zeroes are asked for\n");
    build_good();
    put64(PHOFF + 0x20, CODE_BYTES);
    put64(PHOFF + 0x28, CODE_BYTES + 4096);
    check(elf_check(file, sizeof file, LOW, HIGH, &image) == ELF_OK, "it is accepted");
    check(image.segments[0].mem_bytes > image.segments[0].file_bytes,
          "and the extra space is reported");

    printf("a segment asking for memory a program may not have is refused\n");
    build_good();
    put64(PHOFF + 0x10, 0xFFFFFFFF80000000ull);
    put64(0x18, 0xFFFFFFFF80000000ull);
    check(run() == ELF_SEGMENT_OUT_OF_RANGE, "the kernel's own address");
    build_good();
    put64(PHOFF + 0x10, LOW - PAGE_ALIGNED_STEP);
    put64(0x18, LOW - PAGE_ALIGNED_STEP);
    check(run() == ELF_SEGMENT_OUT_OF_RANGE, "below where a program may live");
    build_good();
    put64(PHOFF + 0x28, HIGH);
    check(run() == ELF_SEGMENT_OUT_OF_RANGE, "a length reaching past the top");
    build_good();
    put64(PHOFF + 0x10, 0xFFFFFFFFFFFFF000ull);
    put64(PHOFF + 0x28, 0x8000);
    check(run() == ELF_SEGMENT_OUT_OF_RANGE, "an address and size that wrap");

    printf("two segments wanting the same memory are refused\n");
    build_good();
    put16(0x38, 2);
    put32(PHOFF + 56 + 0x00, 1);
    put32(PHOFF + 56 + 0x04, ELF_R | ELF_W);
    put64(PHOFF + 56 + 0x08, CODE_AT);
    put64(PHOFF + 56 + 0x10, LOW + 8);   /* inside the first one */
    put64(PHOFF + 56 + 0x20, 8);
    put64(PHOFF + 56 + 0x28, 8);
    check(run() == ELF_SEGMENTS_OVERLAP, "an overlap is caught");

    printf("two segments that only touch are fine\n");
    build_good();
    put16(0x38, 2);
    put32(PHOFF + 56 + 0x00, 1);
    put32(PHOFF + 56 + 0x04, ELF_R | ELF_W);
    put64(PHOFF + 56 + 0x08, CODE_AT);
    put64(PHOFF + 56 + 0x10, LOW + CODE_BYTES);  /* exactly after the first */
    put64(PHOFF + 56 + 0x20, 8);
    put64(PHOFF + 56 + 0x28, 8);
    check(elf_check(file, sizeof file, LOW, HIGH, &image) == ELF_OK, "both are accepted");
    check(image.count == 2, "and both are reported");
    check(image.highest == LOW + CODE_BYTES + 8, "the highest covers both");

    printf("a segment that is not loadable is skipped, not refused\n");
    build_good();
    put16(0x38, 2);
    put32(PHOFF + 56 + 0x00, 0x6474E551);  /* a stack note, which a linker emits */
    put64(PHOFF + 56 + 0x28, 0);
    check(elf_check(file, sizeof file, LOW, HIGH, &image) == ELF_OK, "the file is accepted");
    check(image.count == 1, "and only the loadable segment is reported");

    printf("a file that starts somewhere it never loads is refused\n");
    build_good();
    put64(0x18, LOW + 0x10000);
    check(run() == ELF_BAD_ENTRY, "an entry point outside every segment");
    build_good();
    put64(0x18, LOW + CODE_BYTES);
    check(run() == ELF_BAD_ENTRY, "an entry one byte past the end of the only segment");
    build_good();
    put32(PHOFF + 0x04, ELF_R | ELF_W);
    check(run() == ELF_BAD_ENTRY, "an entry in a segment that will not be executable");

    printf("a file with nothing to load is refused\n");
    build_good();
    put32(PHOFF + 0x00, 4);  /* a note, not a load */
    check(run() == ELF_NO_LOADABLE_SEGMENTS, "no loadable segments");
    build_good();
    put64(PHOFF + 0x28, 0);
    check(run() == ELF_NO_LOADABLE_SEGMENTS, "a loadable segment asking for nothing");

    printf("too many segments is refused rather than overrunning the list\n");
    build_good();
    put16(0x38, ELF_MAX_SEGMENTS + 2);
    for (uint64_t i = 1; i < ELF_MAX_SEGMENTS + 2; i++) {
        uint64_t at = PHOFF + i * 56;
        put32(at + 0x00, 1);
        put32(at + 0x04, ELF_R);
        put64(at + 0x08, CODE_AT);
        put64(at + 0x10, LOW + 0x1000 * i);
        put64(at + 0x20, 8);
        put64(at + 0x28, 8);
    }
    check(run() == ELF_TOO_MANY_SEGMENTS, "the ninth segment is refused");

    printf("every reason has words\n");
    check(elf_result_text(ELF_OK)[0] != '\0', "ok has text");
    check(elf_result_text(ELF_SEGMENTS_OVERLAP)[0] != '\0', "so does an overlap");
    check(elf_result_text((enum elf_result)99)[0] != '\0', "and so does a value that is not one");

    printf(failures == 0 ? "\nELF reader checks passed\n"
                         : "\nELF reader checks FAILED\n");
    return failures == 0 ? 0 : 1;
}
