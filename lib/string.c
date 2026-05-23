#include <xbks/string.h>

void *memset(void *dest, int value, size_t count) {
    unsigned char *out = dest;

    for (size_t i = 0; i < count; ++i) {
        out[i] = (unsigned char)value;
    }

    return dest;
}

void *memcpy(void *dest, const void *src, size_t count) {
    unsigned char *out = dest;
    const unsigned char *in = src;

    for (size_t i = 0; i < count; ++i) {
        out[i] = in[i];
    }

    return dest;
}

void *memmove(void *dest, const void *src, size_t count) {
    unsigned char *out = dest;
    const unsigned char *in = src;

    if (out == in || count == 0) {
        return dest;
    }

    if (out < in) {
        for (size_t i = 0; i < count; ++i) {
            out[i] = in[i];
        }
    } else {
        for (size_t i = count; i > 0; --i) {
            out[i - 1] = in[i - 1];
        }
    }

    return dest;
}

int memcmp(const void *lhs, const void *rhs, size_t count) {
    const unsigned char *left = lhs;
    const unsigned char *right = rhs;

    for (size_t i = 0; i < count; ++i) {
        if (left[i] != right[i]) {
            return (int)left[i] - (int)right[i];
        }
    }

    return 0;
}

size_t strlen(const char *text) {
    size_t length = 0;

    while (text[length] != '\0') {
        ++length;
    }

    return length;
}

int strcmp(const char *lhs, const char *rhs) {
    while (*lhs != '\0' && *lhs == *rhs) {
        ++lhs;
        ++rhs;
    }

    return (int)(unsigned char)*lhs - (int)(unsigned char)*rhs;
}

int strncmp(const char *lhs, const char *rhs, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const unsigned char left = (unsigned char)lhs[i];
        const unsigned char right = (unsigned char)rhs[i];

        if (left != right || left == '\0') {
            return (int)left - (int)right;
        }
    }

    return 0;
}
