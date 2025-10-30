/*
 * UTF-8 Handling Library for Tiny-CLJ
 * 
 * A minimal UTF-8 library for Tiny-CLJ parser needs:
 * - UTF-8 validation
 * - Codepoint iteration
 * - String length calculation
 * - Unicode symbol character recognition
 * - Unicode delimiter detection
 * 
 * This implementation focuses on correctness and minimal code size.
 * All functions are implemented as static inline for zero overhead.
 */

#ifndef TINY_CLJ_UTF8_H
#define TINY_CLJ_UTF8_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UTF-8 validation and iteration functions */

// Internal helper functions
static inline bool utf8_is_continuation_byte(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

static inline int utf8_sequence_length(unsigned char first_byte) {
    if ((first_byte & 0x80) == 0x00) return 1;  // ASCII
    if ((first_byte & 0xE0) == 0xC0) return 2;  // 2-byte sequence
    if ((first_byte & 0xF0) == 0xE0) return 3;  // 3-byte sequence
    if ((first_byte & 0xF8) == 0xF0) return 4;  // 4-byte sequence
    return 0;  // Invalid
}

/**
 * @brief Check if a string is valid UTF-8
 * @param s String to validate
 * @return true if valid UTF-8, false otherwise
 */
static inline bool utf8valid(const char *s) {
    if (!s) return false;
    
    while (*s) {
        unsigned char first = (unsigned char)*s;
        int len = utf8_sequence_length(first);
        
        if (len == 0) return false;  // Invalid first byte
        
        // Check continuation bytes
        for (int i = 1; i < len; i++) {
            s++;
            if (!*s || !utf8_is_continuation_byte((unsigned char)*s)) {
                return false;
            }
        }
        
        // Basic validation for overlong sequences and invalid ranges
        if (len == 2 && first < 0xC2) return false;
        if (len == 3 && first == 0xE0 && (unsigned char)s[1] < 0xA0) return false;
        if (len == 3 && first == 0xED && (unsigned char)s[1] >= 0xA0) return false;
        if (len == 4 && first == 0xF0 && (unsigned char)s[1] < 0x90) return false;
        if (len == 4 && first >= 0xF5) return false;
        
        s++;
    }
    return true;
}

/**
 * @brief Get the number of UTF-8 codepoints in a string
 * @param s UTF-8 string
 * @return Number of codepoints, or 0 if invalid UTF-8
 */
static inline size_t utf8len(const char *s) {
    if (!s || !utf8valid(s)) return 0;
    
    size_t count = 0;
    while (*s) {
        unsigned char first = (unsigned char)*s;
        int len = utf8_sequence_length(first);
        if (len == 0) return 0;
        count++;
        s += len;
    }
    return count;
}

/**
 * @brief Advance over a single UTF-8 codepoint
 * @param s Pointer to current position in UTF-8 string
 * @param out_cp Pointer to store codepoint (can be NULL)
 * @return Pointer to start of next codepoint, or NULL if invalid
 */
static inline const char *utf8codepoint(const char *s, int *out_cp) {
    if (!s || !*s) return s;
    
    unsigned char first = (unsigned char)*s;
    int len = utf8_sequence_length(first);
    
    if (len == 0) return NULL;  // Invalid sequence
    
    // Decode codepoint
    int cp = 0;
    if (len == 1) {
        cp = first;
    } else {
        cp = first & (0xFF >> (len + 1));
        for (int i = 1; i < len; i++) {
            s++;
            if (!*s || !utf8_is_continuation_byte((unsigned char)*s)) {
                return NULL;
            }
            cp = (cp << 6) | (*s & 0x3F);
        }
    }
    
    if (out_cp) *out_cp = cp;
    return s + 1;
}

/**
 * @brief Check if a codepoint is a valid symbol character
 * @param cp UTF-8 codepoint
 * @return true if valid for symbols, false otherwise
 */
static inline bool utf8_is_symbol_char(int cp) {
    // Allow Unicode letters, digits, and common symbol characters
    return (cp >= 'a' && cp <= 'z') ||
           (cp >= 'A' && cp <= 'Z') ||
           (cp >= '0' && cp <= '9') ||
           cp == '-' || cp == '_' || cp == '?' || cp == '!' || cp == '/' || cp == '.' ||
           cp == '+' || cp == '*' || cp == '=' || cp == '<' || cp == '>' ||
           cp == '&' || cp == '|' ||
           // Basic Unicode letters (most common ranges)
           (cp >= 0x00C0 && cp <= 0x00FF) ||  // Latin Extended-A
           (cp >= 0x0100 && cp <= 0x017F) ||  // Latin Extended-A
           (cp >= 0x0180 && cp <= 0x024F) ||  // Latin Extended-B
           (cp >= 0x0370 && cp <= 0x03FF) ||  // Greek
           (cp >= 0x0400 && cp <= 0x04FF) ||  // Cyrillic
           (cp >= 0x1F00 && cp <= 0x1FFF) ||  // Greek Extended
           (cp >= 0x2000 && cp <= 0x206F) ||  // General Punctuation
           (cp >= 0x2070 && cp <= 0x209F) ||  // Superscripts and Subscripts
           (cp >= 0x20A0 && cp <= 0x20CF) ||  // Currency Symbols
           (cp >= 0x2100 && cp <= 0x214F) ||  // Letterlike Symbols
           (cp >= 0x2190 && cp <= 0x21FF) ||  // Arrows
           (cp >= 0x2200 && cp <= 0x22FF) ||  // Mathematical Operators
           (cp >= 0x2300 && cp <= 0x23FF) ||  // Miscellaneous Technical
           (cp >= 0x25A0 && cp <= 0x25FF) ||  // Geometric Shapes
           (cp >= 0x2600 && cp <= 0x26FF) ||  // Miscellaneous Symbols
           (cp >= 0x2700 && cp <= 0x27BF) ||  // Dingbats
           (cp >= 0x27C0 && cp <= 0x27EF) ||  // Miscellaneous Mathematical Symbols-A
           (cp >= 0x27F0 && cp <= 0x27FF) ||  // Supplemental Arrows-A
           (cp >= 0x2800 && cp <= 0x28FF) ||  // Braille Patterns
           (cp >= 0x2900 && cp <= 0x297F) ||  // Supplemental Arrows-B
           (cp >= 0x2980 && cp <= 0x29FF) ||  // Miscellaneous Mathematical Symbols-B
           (cp >= 0x2A00 && cp <= 0x2AFF) ||  // Supplemental Mathematical Operators
           (cp >= 0x2B00 && cp <= 0x2BFF) ||  // Miscellaneous Symbols and Arrows
           (cp >= 0x2C00 && cp <= 0x2C5F) ||  // Glagolitic
           (cp >= 0x2C60 && cp <= 0x2C7F) ||  // Latin Extended-C
           (cp >= 0x2C80 && cp <= 0x2CFF) ||  // Coptic
           (cp >= 0x2D00 && cp <= 0x2D2F) ||  // Georgian Supplement
           (cp >= 0x2D30 && cp <= 0x2D7F) ||  // Tifinagh
           (cp >= 0x2D80 && cp <= 0x2DDF) ||  // Ethiopic Extended
           (cp >= 0x2DE0 && cp <= 0x2DFF) ||  // Cyrillic Extended-A
           (cp >= 0x2E00 && cp <= 0x2E7F) ||  // Supplemental Punctuation
           (cp >= 0x2E80 && cp <= 0x2EFF) ||  // CJK Radicals Supplement
           (cp >= 0x2F00 && cp <= 0x2FDF) ||  // Kangxi Radicals
           (cp >= 0x2FF0 && cp <= 0x2FFF) ||  // Ideographic Description Characters
           (cp >= 0x3000 && cp <= 0x303F) ||  // CJK Symbols and Punctuation
           (cp >= 0x3040 && cp <= 0x309F) ||  // Hiragana
           (cp >= 0x30A0 && cp <= 0x30FF) ||  // Katakana
           (cp >= 0x3100 && cp <= 0x312F) ||  // Bopomofo
           (cp >= 0x3130 && cp <= 0x318F) ||  // Hangul Compatibility Jamo
           (cp >= 0x3190 && cp <= 0x319F) ||  // Kanbun
           (cp >= 0x31A0 && cp <= 0x31BF) ||  // Bopomofo Extended
           (cp >= 0x31C0 && cp <= 0x31EF) ||  // CJK Strokes
           (cp >= 0x31F0 && cp <= 0x31FF) ||  // Katakana Phonetic Extensions
           (cp >= 0x3200 && cp <= 0x32FF) ||  // Enclosed CJK Letters and Months
           (cp >= 0x3300 && cp <= 0x33FF) ||  // CJK Compatibility
           (cp >= 0x3400 && cp <= 0x4DBF) ||  // CJK Unified Ideographs Extension A
           (cp >= 0x4DC0 && cp <= 0x4DFF) ||  // Yijing Hexagram Symbols
           (cp >= 0x4E00 && cp <= 0x9FFF) ||  // CJK Unified Ideographs
           (cp >= 0xA000 && cp <= 0xA48F) ||  // Yi Syllables
           (cp >= 0xA490 && cp <= 0xA4CF) ||  // Yi Radicals
           (cp >= 0xA4D0 && cp <= 0xA4FF) ||  // Lisu
           (cp >= 0xA500 && cp <= 0xA63F) ||  // Vai
           (cp >= 0xA640 && cp <= 0xA69F) ||  // Cyrillic Extended-B
           (cp >= 0xA6A0 && cp <= 0xA6FF) ||  // Bamum
           (cp >= 0xA700 && cp <= 0xA71F) ||  // Modifier Tone Letters
           (cp >= 0xA720 && cp <= 0xA7FF) ||  // Latin Extended-D
           (cp >= 0xA800 && cp <= 0xA82F) ||  // Syloti Nagri
           (cp >= 0xA830 && cp <= 0xA83F) ||  // Common Indic Number Forms
           (cp >= 0xA840 && cp <= 0xA87F) ||  // Phags-pa
           (cp >= 0xA880 && cp <= 0xA8DF) ||  // Saurashtra
           (cp >= 0xA8E0 && cp <= 0xA8FF) ||  // Devanagari Extended
           (cp >= 0xA900 && cp <= 0xA92F) ||  // Kayah Li
           (cp >= 0xA930 && cp <= 0xA95F) ||  // Rejang
           (cp >= 0xA960 && cp <= 0xA97F) ||  // Hangul Jamo Extended-A
           (cp >= 0xA980 && cp <= 0xA9DF) ||  // Javanese
           (cp >= 0xA9E0 && cp <= 0xA9FF) ||  // Myanmar Extended-B
           (cp >= 0xAA00 && cp <= 0xAA5F) ||  // Cham
           (cp >= 0xAA60 && cp <= 0xAA7F) ||  // Myanmar Extended-A
           (cp >= 0xAA80 && cp <= 0xAADF) ||  // Tai Viet
           (cp >= 0xAAE0 && cp <= 0xAAFF) ||  // Meetei Mayek Extensions
           (cp >= 0xAB00 && cp <= 0xAB2F) ||  // Ethiopic Extended-A
           (cp >= 0xAB30 && cp <= 0xAB6F) ||  // Latin Extended-E
           (cp >= 0xAB70 && cp <= 0xABBF) ||  // Cherokee Supplement
           (cp >= 0xABC0 && cp <= 0xABFF) ||  // Meetei Mayek
           (cp >= 0xAC00 && cp <= 0xD7AF) ||  // Hangul Syllables
           (cp >= 0xD7B0 && cp <= 0xD7FF) ||  // Hangul Jamo Extended-B
           (cp >= 0xD800 && cp <= 0xDB7F) ||  // High Surrogates
           (cp >= 0xDB80 && cp <= 0xDBFF) ||  // High Private Use Surrogates
           (cp >= 0xDC00 && cp <= 0xDFFF) ||  // Low Surrogates
           (cp >= 0xE000 && cp <= 0xF8FF) ||  // Private Use Area
           (cp >= 0xF900 && cp <= 0xFAFF) ||  // CJK Compatibility Ideographs
           (cp >= 0xFB00 && cp <= 0xFB4F) ||  // Alphabetic Presentation Forms
           (cp >= 0xFB50 && cp <= 0xFDFF) ||  // Arabic Presentation Forms-A
           (cp >= 0xFE00 && cp <= 0xFE0F) ||  // Variation Selectors
           (cp >= 0xFE10 && cp <= 0xFE1F) ||  // Vertical Forms
           (cp >= 0xFE20 && cp <= 0xFE2F) ||  // Combining Half Marks
           (cp >= 0xFE30 && cp <= 0xFE4F) ||  // CJK Compatibility Forms
           (cp >= 0xFE50 && cp <= 0xFE6F) ||  // Small Form Variants
           (cp >= 0xFE70 && cp <= 0xFEFF) ||  // Arabic Presentation Forms-B
           (cp >= 0xFF00 && cp <= 0xFFEF) ||  // Halfwidth and Fullwidth Forms
           (cp >= 0xFFF0 && cp <= 0xFFFF);    // Specials
}

/**
 * @brief Check if a codepoint is a delimiter character
 * @param cp UTF-8 codepoint
 * @return true if delimiter, false otherwise
 */
static inline bool utf8_is_delimiter(int cp) {
    // Standard ASCII delimiters
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') return true;
    if (cp == '(' || cp == ')' || cp == '[' || cp == ']') return true;
    if (cp == '{' || cp == '}' || cp == '"' || cp == ';') return true;
    if (cp == ',' || cp == '@' || cp == '^' || cp == '`') return true;
    if (cp == '~' || cp == '\'') return true;
    
    // Unicode whitespace and punctuation
    return (cp >= 0x2000 && cp <= 0x200F) ||  // General Punctuation
           (cp >= 0x2010 && cp <= 0x201F) ||  // General Punctuation
           (cp >= 0x2020 && cp <= 0x2027) ||  // General Punctuation
           (cp >= 0x2028 && cp <= 0x2029) ||  // General Punctuation (line/para separators)
           (cp >= 0x202A && cp <= 0x202E) ||  // General Punctuation
           (cp >= 0x202F && cp <= 0x202F) ||  // General Punctuation (narrow no-break space)
           (cp >= 0x2030 && cp <= 0x2043) ||  // General Punctuation
           (cp >= 0x2044 && cp <= 0x2044) ||  // General Punctuation (fraction slash)
           (cp >= 0x2045 && cp <= 0x2051) ||  // General Punctuation
           (cp >= 0x2052 && cp <= 0x205E) ||  // General Punctuation
           (cp >= 0x205F && cp <= 0x205F) ||  // General Punctuation (medium mathematical space)
           (cp >= 0x2060 && cp <= 0x2064) ||  // General Punctuation
           (cp >= 0x206A && cp <= 0x206F) ||  // General Punctuation
           (cp >= 0x3000 && cp <= 0x3000) ||  // Ideographic Space
           (cp >= 0x3001 && cp <= 0x3003) ||  // CJK Symbols and Punctuation
           (cp >= 0x3004 && cp <= 0x3004) ||  // CJK Symbols and Punctuation
           (cp >= 0x3005 && cp <= 0x3005) ||  // CJK Symbols and Punctuation
           (cp >= 0x3006 && cp <= 0x3006) ||  // CJK Symbols and Punctuation
           (cp >= 0x3007 && cp <= 0x3007) ||  // CJK Symbols and Punctuation
           (cp >= 0x3008 && cp <= 0x3011) ||  // CJK Symbols and Punctuation
           (cp >= 0x3012 && cp <= 0x3013) ||  // CJK Symbols and Punctuation
           (cp >= 0x3014 && cp <= 0x301F) ||  // CJK Symbols and Punctuation
           (cp >= 0x3020 && cp <= 0x3020) ||  // CJK Symbols and Punctuation
           (cp >= 0x3030 && cp <= 0x3030) ||  // CJK Symbols and Punctuation
           (cp >= 0x3031 && cp <= 0x3035) ||  // CJK Symbols and Punctuation
           (cp >= 0x3036 && cp <= 0x3037) ||  // CJK Symbols and Punctuation
           (cp >= 0x3038 && cp <= 0x303A) ||  // CJK Symbols and Punctuation
           (cp >= 0x303B && cp <= 0x303C) ||  // CJK Symbols and Punctuation
           (cp >= 0x303D && cp <= 0x303F) ||  // CJK Symbols and Punctuation
           (cp >= 0xFEFF && cp <= 0xFEFF);    // Zero Width No-Break Space (BOM)
}

#ifdef __cplusplus
}
#endif

#endif /* TINY_CLJ_UTF8_H */