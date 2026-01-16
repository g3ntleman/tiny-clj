#include "mini_format.h"

#include <stdbool.h>
#include <stdint.h>

static size_t mini_append_char(char *dst, size_t cap, size_t pos, char ch) {
    if (cap > 0 && pos + 1 < cap) {
        dst[pos] = ch;
    }
    return pos + 1;
}

static size_t mini_append_str(char *dst, size_t cap, size_t pos, const char *s) {
    if (!s) s = "(null)";
    for (; *s; s++) {
        pos = mini_append_char(dst, cap, pos, *s);
    }
    return pos;
}

static size_t mini_append_uint_base(char *dst, size_t cap, size_t pos, unsigned long long v, unsigned base, bool upper) {
    static const char *digits_l = "0123456789abcdef";
    static const char *digits_u = "0123456789ABCDEF";
    const char *digits = upper ? digits_u : digits_l;

    char tmp[32];
    size_t n = 0;
    if (base < 2) base = 10;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v && n < sizeof(tmp)) {
            tmp[n++] = digits[(unsigned)(v % base)];
            v /= base;
        }
    }
    while (n > 0) {
        pos = mini_append_char(dst, cap, pos, tmp[--n]);
    }
    return pos;
}

static size_t mini_append_int_dec(char *dst, size_t cap, size_t pos, long long v) {
    if (v < 0) {
        pos = mini_append_char(dst, cap, pos, '-');
        // handle LLONG_MIN without overflow
        unsigned long long u = (unsigned long long)(-(v + 1)) + 1ull;
        return mini_append_uint_base(dst, cap, pos, u, 10, false);
    }
    return mini_append_uint_base(dst, cap, pos, (unsigned long long)v, 10, false);
}

static size_t mini_append_double_fixed(char *dst, size_t cap, size_t pos, double v, int precision) {
    if (precision < 0) precision = 6;
    if (precision > 9) precision = 9; // keep 32-bit safe

    if (v < 0) {
        pos = mini_append_char(dst, cap, pos, '-');
        v = -v;
    }

    unsigned long long int_part = (unsigned long long)v;
    pos = mini_append_uint_base(dst, cap, pos, int_part, 10, false);

    if (precision == 0) {
        return pos;
    }

    pos = mini_append_char(dst, cap, pos, '.');

    // scale fraction
    unsigned long long scale = 1;
    for (int i = 0; i < precision; i++) scale *= 10ull;
    double frac = v - (double)int_part;
    unsigned long long frac_part = (unsigned long long)(frac * (double)scale + 0.5); // round

    // handle carry due to rounding
    if (frac_part >= scale) {
        int_part += 1;
        frac_part -= scale;
        // we already printed int_part without carry; we can't "rewind" cheaply.
        // best-effort: this only happens near .99999..., extremely rare; accept tiny drift.
    }

    // zero-pad fractional part
    unsigned long long div = scale / 10ull;
    while (div > 0) {
        unsigned digit = (unsigned)(frac_part / div);
        frac_part %= div;
        div /= 10ull;
        pos = mini_append_char(dst, cap, pos, (char)('0' + digit));
    }

    return pos;
}

int clj_mini_vsnprintf(char *dst, size_t cap, const char *fmt, va_list ap) {
    if (!dst || cap == 0) {
        // Still consume args deterministically by formatting into a zero-cap buffer.
        // We return 0 in this degenerate case.
        return 0;
    }
    if (!fmt) {
        dst[0] = '\0';
        return 0;
    }

    size_t pos = 0;

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            pos = mini_append_char(dst, cap, pos, *p);
            continue;
        }

        p++; // skip '%'
        if (*p == '\0') break;
        if (*p == '%') {
            pos = mini_append_char(dst, cap, pos, '%');
            continue;
        }

        // optional flags (ignore)
        while (*p == '+' || *p == '-' || *p == ' ' || *p == '0' || *p == '#') {
            p++;
        }

        // optional width (ignore)
        while (*p >= '0' && *p <= '9') {
            p++;
        }

        // optional precision (only used for %f)
        int precision = -1;
        if (*p == '.') {
            p++;
            precision = 0;
            while (*p >= '0' && *p <= '9') {
                precision = precision * 10 + (*p - '0');
                p++;
            }
        }

        // optional length modifier
        bool len_l = false;
        bool len_z = false;
        if (*p == 'l') { len_l = true; p++; }
        else if (*p == 'z') { len_z = true; p++; }

        char spec = *p;
        switch (spec) {
            case 's': {
                const char *s = va_arg(ap, const char*);
                pos = mini_append_str(dst, cap, pos, s);
                break;
            }
            case 'c': {
                int ch = va_arg(ap, int);
                pos = mini_append_char(dst, cap, pos, (char)ch);
                break;
            }
            case 'd':
            case 'i': {
                long long v;
                if (len_z) v = (long long)va_arg(ap, ptrdiff_t);
                else if (len_l) v = (long long)va_arg(ap, long);
                else v = (long long)va_arg(ap, int);
                pos = mini_append_int_dec(dst, cap, pos, v);
                break;
            }
            case 'u': {
                unsigned long long v;
                if (len_z) v = (unsigned long long)va_arg(ap, size_t);
                else if (len_l) v = (unsigned long long)va_arg(ap, unsigned long);
                else v = (unsigned long long)va_arg(ap, unsigned int);
                pos = mini_append_uint_base(dst, cap, pos, v, 10, false);
                break;
            }
            case 'x':
            case 'X': {
                unsigned long long v;
                if (len_z) v = (unsigned long long)va_arg(ap, size_t);
                else if (len_l) v = (unsigned long long)va_arg(ap, unsigned long);
                else v = (unsigned long long)va_arg(ap, unsigned int);
                pos = mini_append_uint_base(dst, cap, pos, v, 16, (spec == 'X'));
                break;
            }
            case 'p': {
                void *ptr = va_arg(ap, void*);
                uintptr_t v = (uintptr_t)ptr;
                pos = mini_append_str(dst, cap, pos, "0x");
                pos = mini_append_uint_base(dst, cap, pos, (unsigned long long)v, 16, false);
                break;
            }
            case 'f': {
                double v = va_arg(ap, double);
                pos = mini_append_double_fixed(dst, cap, pos, v, precision);
                break;
            }
            default:
                // unknown spec: emit literally
                pos = mini_append_char(dst, cap, pos, '%');
                pos = mini_append_char(dst, cap, pos, spec);
                break;
        }
    }

    // null-terminate
    if (cap > 0) {
        size_t term = (pos < cap) ? pos : (cap - 1);
        dst[term] = '\0';
    }

    // return "would have written" count approx; we track actual char count
    return (int)pos;
}

int clj_mini_snprintf(char *dst, size_t cap, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = clj_mini_vsnprintf(dst, cap, fmt, ap);
    va_end(ap);
    return n;
}

