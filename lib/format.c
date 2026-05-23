#include <stdarg.h>
#include <xbks/format.h>
#include <xbks/string.h>

struct format_sink {
    char *buffer;
    size_t capacity;
    size_t length;
};

static void sink_putc(struct format_sink *sink, char ch) {
    if (sink->capacity != 0 && sink->length + 1 < sink->capacity) {
        sink->buffer[sink->length] = ch;
    }

    ++sink->length;
}

static void sink_write(struct format_sink *sink, const char *text) {
    while (*text != '\0') {
        sink_putc(sink, *text);
        ++text;
    }
}

static void sink_write_unsigned(
    struct format_sink *sink,
    uint64_t value,
    unsigned int base,
    bool uppercase
) {
    char digits[32];
    size_t count = 0;
    const char *alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (value == 0) {
        sink_putc(sink, '0');
        return;
    }

    while (value != 0) {
        digits[count] = alphabet[value % base];
        value /= base;
        ++count;
    }

    while (count != 0) {
        --count;
        sink_putc(sink, digits[count]);
    }
}

static void sink_write_signed(struct format_sink *sink, int64_t value) {
    uint64_t magnitude;

    if (value < 0) {
        sink_putc(sink, '-');
        magnitude = (uint64_t)(-(value + 1)) + 1;
    } else {
        magnitude = (uint64_t)value;
    }

    sink_write_unsigned(sink, magnitude, 10, false);
}

int xbks_vsnprintf(char *buffer, size_t buffer_size, const char *fmt, va_list args) {
    struct format_sink sink = {
        .buffer = buffer,
        .capacity = buffer_size,
        .length = 0,
    };

    for (size_t i = 0; fmt[i] != '\0'; ++i) {
        if (fmt[i] != '%') {
            sink_putc(&sink, fmt[i]);
            continue;
        }

        ++i;
        if (fmt[i] == '\0') {
            sink_putc(&sink, '%');
            break;
        }

        bool long_value = false;
        bool long_long_value = false;

        if (fmt[i] == 'l') {
            long_value = true;
            ++i;
            if (fmt[i] == 'l') {
                long_long_value = true;
                ++i;
            }
        } else if (fmt[i] == 'z') {
            long_long_value = true;
            ++i;
        }

        switch (fmt[i]) {
        case '%':
            sink_putc(&sink, '%');
            break;
        case 's': {
            const char *value = va_arg(args, const char *);
            sink_write(&sink, value != 0 ? value : "(null)");
            break;
        }
        case 'c': {
            int value = va_arg(args, int);
            sink_putc(&sink, (char)value);
            break;
        }
        case 'd':
        case 'i':
            if (long_long_value) {
                sink_write_signed(&sink, va_arg(args, long long));
            } else if (long_value) {
                sink_write_signed(&sink, va_arg(args, long));
            } else {
                sink_write_signed(&sink, va_arg(args, int));
            }
            break;
        case 'u':
            if (long_long_value) {
                sink_write_unsigned(&sink, va_arg(args, unsigned long long), 10, false);
            } else if (long_value) {
                sink_write_unsigned(&sink, va_arg(args, unsigned long), 10, false);
            } else {
                sink_write_unsigned(&sink, va_arg(args, unsigned int), 10, false);
            }
            break;
        case 'x':
        case 'X':
            if (long_long_value) {
                sink_write_unsigned(&sink, va_arg(args, unsigned long long), 16, fmt[i] == 'X');
            } else if (long_value) {
                sink_write_unsigned(&sink, va_arg(args, unsigned long), 16, fmt[i] == 'X');
            } else {
                sink_write_unsigned(&sink, va_arg(args, unsigned int), 16, fmt[i] == 'X');
            }
            break;
        case 'p': {
            uintptr_t value = (uintptr_t)va_arg(args, void *);
            sink_write(&sink, "0x");
            sink_write_unsigned(&sink, value, 16, false);
            break;
        }
        default:
            sink_putc(&sink, '%');
            sink_putc(&sink, fmt[i]);
            break;
        }
    }

    if (buffer_size != 0) {
        const size_t terminator = sink.length < buffer_size ? sink.length : buffer_size - 1;
        buffer[terminator] = '\0';
    }

    return sink.length > (size_t)INT32_MAX ? INT32_MAX : (int)sink.length;
}

int xbks_snprintf(char *buffer, size_t buffer_size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int written = xbks_vsnprintf(buffer, buffer_size, fmt, args);
    va_end(args);
    return written;
}
