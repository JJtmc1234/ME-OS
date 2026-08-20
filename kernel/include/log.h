/* Boot stage logging.
 *
 * Two sinks, both harmless if nothing is listening:
 *   - port 0xE9, QEMU's debug console
 *   - COM1 at 0x3F8, a real serial port, which is what a physical machine
 *     or a QEMU -serial capture will show
 *
 * This is a diagnostic channel, not a console. It has no input, no cursor,
 * and no formatting beyond decimal numbers.
 */
#ifndef ME_LOG_H
#define ME_LOG_H

#include <stdint.h>

/* Safe to call before anything else is set up. */
void log_init(void);

void log_str(const char *s);
void log_dec(uint64_t value);

/* One line: "me-os: <stage>". */
void log_stage(const char *stage);

#endif /* ME_LOG_H */
