#include "timer.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43

/* Channel 0, access low then high byte, mode 2, binary counting. */
#define PIT_MODE_RATE_GENERATOR 0x34
/* Latch channel 0's current count so both bytes come from the same instant. */
#define PIT_LATCH_CHANNEL0 0x00

/* A divisor of 0 means the full 16 bit range, the longest period the counter
 * offers. Longer is safer here: the poll loop only has to look at the counter
 * more often than it wraps. */
#define PIT_PERIOD 65536u

static void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint16_t read_counter(void)
{
    outb(PIT_COMMAND, PIT_LATCH_CHANNEL0);
    uint8_t low = inb(PIT_CHANNEL0);
    uint8_t high = inb(PIT_CHANNEL0);
    return (uint16_t)((uint16_t)high << 8 | low);
}

static uint16_t last_count;
static int started;

void timer_init(void)
{
    outb(PIT_COMMAND, PIT_MODE_RATE_GENERATOR);
    outb(PIT_CHANNEL0, 0x00);  /* divisor low byte */
    outb(PIT_CHANNEL0, 0x00);  /* divisor high byte, so the full range */
    last_count = read_counter();
    started = 1;
}

uint64_t timer_elapsed_between(uint16_t previous, uint16_t current, uint32_t period)
{
    /* The counter counts down, so an ordinary step makes it smaller. When it
     * has grown instead, it wrapped past zero and started again. */
    if (current <= previous) {
        return (uint64_t)(previous - current);
    }
    return (uint64_t)period - (uint64_t)current + (uint64_t)previous;
}

uint64_t timer_poll(void)
{
    if (!started) {
        return 0;
    }

    const uint16_t current = read_counter();
    const uint64_t elapsed = timer_elapsed_between(last_count, current, PIT_PERIOD);
    last_count = current;
    return elapsed;
}
