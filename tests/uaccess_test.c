/* Host tests for M32 user pointer validation.
 *
 * This is the security boundary. A system call arrives with numbers in
 * registers, some of them addresses the program chose, and the kernel is
 * running with that program's page tables loaded but at privilege zero. At
 * privilege zero the user bit stops protecting anything: every mapping in the
 * address space is readable and writable to the kernel, including the kernel
 * half the program itself cannot touch. So a handler that followed a user
 * pointer directly would let any program read or write any part of the kernel
 * by passing the right number.
 *
 * Every case below is one a hostile program would actually try.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uaccess.h"

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

void vmm_flush(uint64_t virt) { (void)virt; }

#define FAKE_BASE 0x200000ull
#define ARENA_PAGES 512u
#define ARENA_BYTES ((uint64_t)ARENA_PAGES * PAGE_SIZE)

static uint8_t *arena;
static uint8_t bitmap[ARENA_PAGES / 8u];
static struct pmm pmm;
static uint64_t hhdm;
static struct addrspace space;

/* Where a pretend program's memory lives. Two pages next to each other, so a
 * copy can be made to run off the end of one into the other. */
#define USER_AT      0x400000ull
#define USER_PAGES   2
#define READONLY_AT  0x410000ull
#define KERNEL_AT    0x420000ull

static uint8_t *kernel_view_of(uint64_t user)
{
    uint64_t phys = 0;
    if (vmm_translate(&space, user, &phys, NULL) != VMM_OK) {
        return NULL;
    }
    return (uint8_t *)(uintptr_t)(phys + hhdm);
}

static void build_space(void)
{
    memset(arena, 0, (size_t)ARENA_BYTES);
    pmm_reset(&pmm, bitmap, sizeof bitmap, FAKE_BASE / PAGE_SIZE, ARENA_PAGES);
    pmm_add_free(&pmm, FAKE_BASE, ARENA_BYTES);
    vmm_create(&space, &pmm, hhdm);

    for (int i = 0; i < USER_PAGES; i++) {
        vmm_map(&space, USER_AT + (uint64_t)i * PAGE_SIZE, pmm_alloc(&pmm),
                PTE_WRITE | PTE_USER);
    }
    /* A page the program may read and not write. */
    vmm_map(&space, READONLY_AT, pmm_alloc(&pmm), PTE_USER);
    /* A page mapped in the program's address space with no user bit, which is
     * what every kernel page looks like from here. */
    vmm_map(&space, KERNEL_AT, pmm_alloc(&pmm), PTE_WRITE);
}

int main(void)
{
    arena = aligned_alloc(PAGE_SIZE, (size_t)ARENA_BYTES);
    if (arena == NULL) {
        printf("could not allocate the test arena\n");
        return 1;
    }
    hhdm = (uint64_t)(uintptr_t)arena - FAKE_BASE;
    build_space();

    char buffer[256];
    static uint8_t big[3 * PAGE_SIZE];

    printf("an ordinary range inside the program's own memory is allowed\n");
    check(uaccess_range_ok(&space, USER_AT, 16, false), "a small read");
    check(uaccess_range_ok(&space, USER_AT, 16, true), "a small write");
    check(uaccess_range_ok(&space, USER_AT, PAGE_SIZE, false), "exactly one page");
    check(uaccess_range_ok(&space, USER_AT, 2 * PAGE_SIZE, false), "both pages");
    check(uaccess_range_ok(&space, USER_AT + PAGE_SIZE - 8, 16, false),
          "a range that straddles the boundary between them");

    printf("a range that leaves the program's memory is refused\n");
    check(!uaccess_range_ok(&space, USER_AT, 2 * PAGE_SIZE + 1, false),
          "one byte past the end of the second page");
    check(!uaccess_range_ok(&space, USER_AT + 2 * PAGE_SIZE, 1, false),
          "the page after the end");
    check(!uaccess_range_ok(&space, 0x999000, 8, false), "an address that maps nothing");
    check(!uaccess_range_ok(&space, 0, 8, false), "a null pointer");

    printf("a kernel page is refused even though the kernel could read it\n");
    /* The whole point. The kernel is at privilege zero with these tables
     * loaded, so the processor would allow this. The check is what does not. */
    check(kernel_view_of(KERNEL_AT) != NULL, "the page really is mapped in this space");
    check(!uaccess_range_ok(&space, KERNEL_AT, 8, false), "and reading it is still refused");
    check(!uaccess_range_ok(&space, KERNEL_AT, 8, true), "as is writing it");
    check(!uaccess_copy_in(&space, buffer, KERNEL_AT, 8), "a copy out of it is refused");

    printf("a read only page cannot be written\n");
    check(uaccess_range_ok(&space, READONLY_AT, 8, false), "the program may read it");
    check(!uaccess_range_ok(&space, READONLY_AT, 8, true), "and may not have it written");
    check(!uaccess_copy_out(&space, READONLY_AT, "x", 1),
          "so the kernel refuses to write to it on the program's behalf");

    printf("a length that wraps past the top of memory is refused\n");
    /* The classic. The sum wraps, so a naive end pointer looks lower than the
     * start and the range appears to be small and harmless. */
    check(!uaccess_range_ok(&space, USER_AT, 0xFFFFFFFFFFFFFFFFull, false),
          "an enormous length");
    check(!uaccess_range_ok(&space, 0xFFFFFFFFFFFFF000ull, 0x2000, false),
          "a high address with a length that carries past the end");
    check(!uaccess_copy_in(&space, buffer, USER_AT, 0xFFFFFFFFFFFFFFF0ull),
          "and a copy with one is refused too");

    printf("nothing at all is always fine and touches nothing\n");
    check(uaccess_range_ok(&space, 0, 0, false), "a zero length range at a null pointer");
    check(uaccess_range_ok(&space, KERNEL_AT, 0, true), "or at a kernel page");
    check(uaccess_copy_in(&space, buffer, 0x999000, 0), "and a zero length copy succeeds");

    printf("a copy in brings back what the program wrote\n");
    uint8_t *first = kernel_view_of(USER_AT);
    memcpy(first, "HELLO FROM USERSPACE", 20);
    memset(buffer, 0, sizeof buffer);
    check(uaccess_copy_in(&space, buffer, USER_AT, 20), "the copy succeeds");
    check(memcmp(buffer, "HELLO FROM USERSPACE", 20) == 0, "with the right bytes");

    printf("a copy that spans two pages is stitched back together\n");
    uint8_t *second = kernel_view_of(USER_AT + PAGE_SIZE);
    memset(first, 'A', PAGE_SIZE);
    memset(second, 'B', PAGE_SIZE);
    memset(buffer, 0, sizeof buffer);
    check(uaccess_copy_in(&space, buffer, USER_AT + PAGE_SIZE - 4, 8),
          "four bytes either side of the boundary");
    check(memcmp(buffer, "AAAABBBB", 8) == 0,
          "and they come back in the right order from the right pages");

    printf("a copy that fails copies nothing at all\n");
    memset(buffer, 'Z', sizeof buffer);
    check(!uaccess_copy_in(&space, buffer, USER_AT + PAGE_SIZE, PAGE_SIZE + 1),
          "a range running off the end is refused");
    check(buffer[0] == 'Z', "and the destination was not touched first");

    printf("a copy out lands in the program's memory and nowhere else\n");
    check(uaccess_copy_out(&space, USER_AT, "WRITTEN", 7), "writing succeeds");
    check(memcmp(first, "WRITTEN", 7) == 0, "and the bytes are there");
    check(uaccess_copy_out(&space, USER_AT + PAGE_SIZE - 3, "123456", 6),
          "a write across the page boundary succeeds");
    check(memcmp(first + PAGE_SIZE - 3, "123", 3) == 0, "the first part landed");
    check(memcmp(second, "456", 3) == 0, "and the rest landed in the next page");

    printf("a big copy over several pages is whole\n");
    memset(first, 'X', PAGE_SIZE);
    memset(second, 'Y', PAGE_SIZE);
    memset(big, 0, sizeof big);
    check(uaccess_copy_in(&space, big, USER_AT, 2 * PAGE_SIZE), "two whole pages copy");
    check(big[0] == 'X' && big[PAGE_SIZE - 1] == 'X', "the first page is all there");
    check(big[PAGE_SIZE] == 'Y' && big[2 * PAGE_SIZE - 1] == 'Y', "and so is the second");

    printf("a string is copied up to its terminator\n");
    memset(first, 0, PAGE_SIZE);
    memcpy(first, "RUN HELLO", 10);
    memset(buffer, 0, sizeof buffer);
    check(uaccess_copy_string(&space, buffer, sizeof buffer, USER_AT), "the copy succeeds");
    check(strcmp(buffer, "RUN HELLO") == 0, "and the string is right");

    printf("a string with no terminator is refused rather than truncated\n");
    memset(first, 'A', PAGE_SIZE);
    memset(second, 'A', PAGE_SIZE);
    check(!uaccess_copy_string(&space, buffer, 32, USER_AT),
          "no terminator within the buffer is a refusal");
    check(buffer[31] == '\0', "and what was written is still terminated");
    check(!uaccess_copy_string(&space, buffer, sizeof buffer, USER_AT + 2 * PAGE_SIZE),
          "a string starting outside the program's memory is refused");
    check(!uaccess_copy_string(&space, buffer, 0, USER_AT), "so is a buffer with no room");

    printf("a string that runs off the end of mapped memory is refused\n");
    /* The terminator would be in the page after the program's last one. */
    memset(second, 'A', PAGE_SIZE);
    check(!uaccess_copy_string(&space, buffer, sizeof buffer, USER_AT + 2 * PAGE_SIZE - 4),
          "walking past the last mapped page stops rather than reading on");

    printf("a string is allowed to end exactly at the last byte of a page\n");
    memset(second, 'B', PAGE_SIZE);
    second[PAGE_SIZE - 1] = '\0';
    check(uaccess_copy_string(&space, buffer, sizeof buffer, USER_AT + 2 * PAGE_SIZE - 5),
          "a terminator on the final byte is found");
    check(strcmp(buffer, "BBBB") == 0, "and the string before it is right");

    printf("no address space means no access\n");
    check(!uaccess_range_ok(NULL, USER_AT, 8, false), "a range check refuses");
    check(!uaccess_copy_in(NULL, buffer, USER_AT, 8), "a copy in refuses");
    check(!uaccess_copy_out(NULL, USER_AT, "x", 1), "a copy out refuses");
    check(!uaccess_copy_string(NULL, buffer, sizeof buffer, USER_AT), "a string copy refuses");

    free(arena);
    printf(failures == 0 ? "\nuser pointer checks passed\n"
                         : "\nuser pointer checks FAILED\n");
    return failures == 0 ? 0 : 1;
}
