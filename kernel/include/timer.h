/* Elapsed time, from the programmable interval timer.
 *
 * Polled, like everything else so far. The PIT's channel 0 counts down at a
 * fixed rate and wraps; reading it often enough and adding up the differences
 * gives elapsed time without an interrupt handler.
 *
 * The wrap arithmetic is a pure function so it can be tested on an ordinary
 * machine, because getting it wrong makes time jump backwards once every
 * 55 milliseconds, which is exactly the kind of bug that hides.
 */
#ifndef ME_TIMER_H
#define ME_TIMER_H

#include <stdint.h>

/* The PIT's input frequency, 1.193182 MHz, as whole hertz. */
#define TIMER_HZ 1193182u

/* Programs channel 0 to free run over its whole 16 bit range, which wraps
 * about every 55 milliseconds. */
void timer_init(void);

/* Counts elapsed between two reads of a counter that counts down and wraps.
 * `period` is the counter's full range. */
uint64_t timer_elapsed_between(uint16_t previous, uint16_t current, uint32_t period);

/* Reads the counter and returns how many counts have passed since the last
 * call. The first call after timer_init returns 0. */
uint64_t timer_poll(void);

#endif /* ME_TIMER_H */
