#include <xbks/io.h>
#include <xbks/serial.h>

enum {
    SERIAL_DATA = 0,
    SERIAL_INTERRUPT_ENABLE = 1,
    SERIAL_FIFO_CONTROL = 2,
    SERIAL_LINE_CONTROL = 3,
    SERIAL_MODEM_CONTROL = 4,
    SERIAL_LINE_STATUS = 5,
};

static bool serial_transmit_empty(uint16_t port) {
    return (xbks_inb((uint16_t)(port + SERIAL_LINE_STATUS)) & 0x20u) != 0;
}

static bool serial_received(uint16_t port) {
    return (xbks_inb((uint16_t)(port + SERIAL_LINE_STATUS)) & 0x01u) != 0;
}

bool xbks_serial_init(uint16_t port) {
    xbks_outb((uint16_t)(port + SERIAL_INTERRUPT_ENABLE), 0x00);
    xbks_outb((uint16_t)(port + SERIAL_LINE_CONTROL), 0x80);
    xbks_outb((uint16_t)(port + SERIAL_DATA), 0x03);
    xbks_outb((uint16_t)(port + SERIAL_INTERRUPT_ENABLE), 0x00);
    xbks_outb((uint16_t)(port + SERIAL_LINE_CONTROL), 0x03);
    xbks_outb((uint16_t)(port + SERIAL_FIFO_CONTROL), 0xc7);
    xbks_outb((uint16_t)(port + SERIAL_MODEM_CONTROL), 0x0b);

    return true;
}

bool xbks_serial_read_byte(uint16_t port, uint8_t *out) {
    if (out == 0 || !serial_received(port)) {
        return false;
    }

    *out = xbks_inb(port);
    return true;
}

void xbks_serial_write_byte(uint16_t port, uint8_t byte) {
    while (!serial_transmit_empty(port)) {
        __asm__ volatile ("pause");
    }

    xbks_outb(port, byte);
}

void xbks_serial_write(uint16_t port, const char *data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (data[i] == '\n') {
            xbks_serial_write_byte(port, '\r');
        }
        xbks_serial_write_byte(port, (uint8_t)data[i]);
    }
}

void xbks_serial_write_string(uint16_t port, const char *text) {
    while (*text != '\0') {
        if (*text == '\n') {
            xbks_serial_write_byte(port, '\r');
        }
        xbks_serial_write_byte(port, (uint8_t)*text);
        ++text;
    }
}
