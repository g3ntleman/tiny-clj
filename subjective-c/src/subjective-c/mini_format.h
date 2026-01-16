#ifndef SUBJECTIVE_C_MINI_FORMAT_H
#define SUBJECTIVE_C_MINI_FORMAT_H

#include <stdarg.h>
#include <stddef.h>

/**
 * Minimal snprintf/vsnprintf replacement designed for embedded size.
 *
 * Supported:
 * - %% %s %c
 * - %d %i %u (optionally with 'l' or 'z' length modifier)
 * - %x %X (optionally with 'l' or 'z')
 * - %p (prints 0x + lowercase hex)
 * - %f with optional precision (e.g. %.2f). Default precision: 6.
 *
 * Not supported (left as literal "%<spec>"):
 * - width, padding, alignment, scientific formats, etc.
 */
int clj_mini_vsnprintf(char *dst, size_t cap, const char *fmt, va_list ap);
int clj_mini_snprintf(char *dst, size_t cap, const char *fmt, ...);

// -----------------------------------------------------------------------------
// Legacy tiny-clj "format_utils.h" helpers (now consolidated here)
// -----------------------------------------------------------------------------
// These are intentionally small, allocation-free string builders used for
// composing error messages without depending on libc printf.

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
    (void)clj_mini_snprintf(number, sizeof(number), "%u", value);
    return format_append(dest, offset, capacity, number);
}

static inline size_t format_append_int(char *dest, size_t offset, size_t capacity, int value) {
    char number[16];
    (void)clj_mini_snprintf(number, sizeof(number), "%d", value);
    return format_append(dest, offset, capacity, number);
}

static inline size_t format_append_ulong(char *dest, size_t offset, size_t capacity, unsigned long value) {
    char number[32];
    (void)clj_mini_snprintf(number, sizeof(number), "%lu", value);
    return format_append(dest, offset, capacity, number);
}

#endif

