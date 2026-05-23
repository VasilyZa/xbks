#ifndef XBKS_IO_H
#define XBKS_IO_H

#include <xbks/types.h>

static inline void xbks_io_wait(void) {
    __asm__ volatile ("outb %%al, $0x80" : : "a" (0));
}

static inline uint8_t xbks_inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a" (value) : "Nd" (port) : "memory");
    return value;
}

static inline void xbks_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a" (value), "Nd" (port) : "memory");
}

static inline void xbks_debugcon_write_byte(uint8_t value) {
    __asm__ volatile ("outb %0, $0xe9" : : "a" (value) : "memory");
}

static inline void xbks_cli(void) {
    __asm__ volatile ("cli" ::: "memory");
}

static inline void xbks_hlt(void) {
    __asm__ volatile ("hlt" ::: "memory");
}

#endif
