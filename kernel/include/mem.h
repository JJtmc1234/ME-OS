/* Freestanding memory primitives.
 *
 * GCC may emit calls to memset, memcpy, memmove and memcmp on its own, even
 * with -ffreestanding and even where the source never mentions them, for
 * things like struct assignment or zeroing a local array. Without these the
 * kernel links today only by luck, and stops linking the moment a future
 * milestone writes ordinary looking C.
 */
#ifndef ME_MEM_H
#define ME_MEM_H

#include <stddef.h>

void *memset(void *dest, int value, size_t count);
void *memcpy(void *restrict dest, const void *restrict src, size_t count);
void *memmove(void *dest, const void *src, size_t count);
int memcmp(const void *a, const void *b, size_t count);

#endif /* ME_MEM_H */
