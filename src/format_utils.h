#ifndef FORMAT_UTILS_H
#define FORMAT_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include "numeric_utils.h"

static inline size_t format_append(char *dest, size_t offset, size_t capacity, const char *text) {
    if (!dest || offset >= capacity) {
        return offset;
    }

    if (!text) {
        text = "";
    }

    size_t remaining = capacity - offset;
    if (remaining == 0) {
        return offset;
    }

    size_t index = 0;
    while (index + 1 < remaining && text[index] != '\0') {
        dest[offset + index] = text[index];
        index++;
    }

    offset += index;
    dest[offset] = '\0';
    return offset;
}

static inline size_t format_append_char(char *dest, size_t offset, size_t capacity, char ch) {
    if (!dest || offset >= capacity) {
        return offset;
    }

    if (capacity - offset <= 1) {
        dest[capacity - 1] = '\0';
        return capacity - 1;
    }

    dest[offset++] = ch;
    dest[offset] = '\0';
    return offset;
}

static inline size_t format_append_uint(char *dest, size_t offset, size_t capacity, unsigned int value) {
    char number[16];
    clj_uitoa(value, number);
    return format_append(dest, offset, capacity, number);
}

static inline size_t format_append_int(char *dest, size_t offset, size_t capacity, int value) {
    char number[16];
    clj_itoa(value, number);
    return format_append(dest, offset, capacity, number);
}

static inline size_t format_append_ulong(char *dest, size_t offset, size_t capacity, unsigned long value) {
    char digits[21];
    size_t index = 0;

    do {
        digits[index++] = (char)('0' + (value % 10UL));
        value /= 10UL;
    } while (value != 0UL && index < sizeof(digits));

    while (index > 0) {
        offset = format_append_char(dest, offset, capacity, digits[--index]);
    }
    return offset;
}

#endif // FORMAT_UTILS_H
