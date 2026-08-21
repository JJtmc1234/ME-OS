/* Turning on the floating point unit.
 *
 * x86-64 guarantees SSE2, so that is what this kernel uses: it is the baseline
 * every 64 bit processor has, the compiler targets it without extra flags, and
 * the calling convention already passes doubles in its registers. x87 is not
 * used at all, and neither is AVX: one way of doing floating point is enough,
 * and mixing them is how state gets corrupted.
 *
 * The processor starts with floating point instructions disabled, so this has
 * to run before any of them execute. Only the modules that need arithmetic are
 * compiled with SSE enabled, and everything they expose to the rest of the
 * kernel takes and returns integers, so nothing can call into floating point
 * code by accident before this has been done.
 *
 * This file is hardware setup and contains no arithmetic. The arithmetic is in
 * geometry.c, which knows nothing about control registers.
 */
#ifndef ME_FPU_H
#define ME_FPU_H

#include <stdbool.h>

/* Enables SSE. False means the processor does not report SSE2, in which case
 * nothing that needs floating point may run. */
bool fpu_init(void);

/* True once fpu_init has succeeded. */
bool fpu_ready(void);

#endif /* ME_FPU_H */
