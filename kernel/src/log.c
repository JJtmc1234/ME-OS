#include "log.h"

#include <stddef.h>

#define DEBUGCON 0xE9
#define COM1     0x3F8

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

void log_init(void)
{
    /* 115200 8N1, FIFOs on, interrupts off. If no UART is present these
     * writes go nowhere, which is the same as doing nothing. */
    outb(COM1 + 1, 0x00);  /* interrupts off while we set the divisor */
    outb(COM1 + 3, 0x80);  /* divisor latch access */
    outb(COM1 + 0, 0x01);  /* divisor low: 115200 baud */
    outb(COM1 + 1, 0x00);  /* divisor high */
    outb(COM1 + 3, 0x03);  /* 8 bits, no parity, one stop bit */
    outb(COM1 + 2, 0xC7);  /* enable and clear the FIFOs */
    outb(COM1 + 4, 0x03);  /* data terminal ready, request to send */
}

static void serial_putc(char c)
{
    /* Bit 5 of the line status register means the transmit buffer is empty.
     * The bounded spin keeps a missing or wedged UART from hanging the boot. */
    for (int spins = 0; spins < 100000; spins++) {
        if ((inb(COM1 + 5) & 0x20) != 0) {
            break;
        }
    }
    outb(COM1, (uint8_t)c);
}

static void putc(char c)
{
    outb(DEBUGCON, (uint8_t)c);
    serial_putc(c);
}

void log_str(const char *s)
{
    if (s == NULL) {
        s = "(null)";
    }
    for (; *s != '\0'; s++) {
        if (*s == '\n') {
            putc('\r');
        }
        putc(*s);
    }
}

void log_dec(uint64_t value)
{
    char digits[20];
    int n = 0;

    if (value == 0) {
        putc('0');
        return;
    }
    while (value > 0 && n < (int)sizeof(digits)) {
        digits[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n > 0) {
        putc(digits[--n]);
    }
}

void log_stage(const char *stage)
{
    log_str("me-os: ");
    log_str(stage);
    log_str("\n");
}
