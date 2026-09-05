/* The x86-64 page table format, as arithmetic.
 *
 * Nothing here touches a page table. This is the bit manipulation the format
 * requires, kept apart from the code that walks memory so that the fiddly half
 * can be tested exhaustively on the development machine, where getting it
 * wrong prints a failure instead of triple faulting a virtual machine.
 *
 * The processor translates a 48 bit virtual address through four tables of 512
 * entries each. Nine bits pick an entry at every level, and the last twelve are
 * the offset inside the page:
 *
 *     47      39 38      30 29      21 20      12 11        0
 *   +----------+----------+----------+----------+-----------+
 *   |   PML4   |   PDPT   |    PD    |    PT    |  offset   |
 *   +----------+----------+----------+----------+-----------+
 *
 * An entry is a 64 bit word holding the physical address of the next table, or
 * of the page itself at the last level, with permission bits in the space the
 * alignment leaves free.
 *
 * See M30 in docs/milestones.md.
 */
#ifndef ME_PAGING_H
#define ME_PAGING_H

#include <stdbool.h>
#include <stdint.h>

/* Entries per table, at every level. 512 entries of 8 bytes is one page. */
#define PAGING_ENTRIES 512u

/* How many levels the walk has. Index 3 is the PML4, index 0 is the PT. */
#define PAGING_LEVELS 4

/* Permission and state bits. Only the ones this kernel sets are named. */
#define PTE_PRESENT   (1ull << 0)
#define PTE_WRITE     (1ull << 1)
#define PTE_USER      (1ull << 2)
#define PTE_ACCESSED  (1ull << 5)
#define PTE_DIRTY     (1ull << 6)
/* Set on a PD or PDPT entry, it means the entry maps a large page directly
 * rather than pointing at another table. This kernel never sets it and has to
 * notice when the bootloader has, because walking into one as though it were a
 * table would read the middle of a mapped page as though it were entries. */
#define PTE_HUGE      (1ull << 7)
#define PTE_GLOBAL    (1ull << 8)
/* Bit 63 forbids execution. Only obeyed once EFER.NXE is on, and a reserved
 * bit fault otherwise, so the kernel checks before it ever sets this. */
#define PTE_NX        (1ull << 63)

/* The physical address lives in bits 51 down to 12. */
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ull

/* Which entry of the table at `level` an address selects.
 *
 * Level 3 is the PML4 and level 0 the page table, so the shift is
 * 12 + 9 * level. Returns 0 for a level out of range rather than shifting by a
 * nonsense amount, which is undefined behaviour and in practice returns
 * whatever the shift instruction felt like. */
uint64_t paging_index(uint64_t virt, int level);

/* Whether the processor will accept this address at all.
 *
 * x86-64 implements 48 bits and requires bits 63 to 48 to be a copy of bit 47.
 * Anything else faults on use. Checked before a mapping is made rather than
 * discovered when something jumps to it. */
bool paging_canonical(uint64_t virt);

/* Builds an entry pointing at a physical address with these flags.
 *
 * The address is masked to the bits the format allows, so a misaligned or
 * oversized address cannot spill into the flags and quietly grant permissions
 * nobody asked for. */
uint64_t paging_entry(uint64_t phys, uint64_t flags);

/* The physical address an entry points at. */
uint64_t paging_entry_addr(uint64_t entry);

static inline bool paging_present(uint64_t entry)
{
    return (entry & PTE_PRESENT) != 0;
}

/* Whether an address sits on a page boundary. */
bool paging_aligned(uint64_t address);

/* The flags an intermediate table entry needs so that a leaf with these flags
 * can actually be reached.
 *
 * The processor takes the permission of a mapping to be the most restrictive
 * of every level on the way down. So a user page under a table entry without
 * the user bit is not reachable from user mode, and a writable page under a
 * read only table entry is not writable. Intermediate entries are therefore
 * always present and writable, and carry the user bit when anything under them
 * is a user page. Restriction is applied at the leaf, which is the only level
 * that describes one page rather than a whole subtree. */
uint64_t paging_parent_flags(uint64_t leaf_flags);

#endif /* ME_PAGING_H */
