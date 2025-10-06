#include "reader.h"
#include <string.h>
#include <ctype.h>
#include "utf8.h"

void reader_init(Reader *reader, const char *src) {
    reader->src = src;
    reader->length = src ? strlen(src) : 0;
    reader->index = 0;
    reader->line = 1;
    reader->column = 1;
}

bool reader_eof(const Reader *reader) {
    return reader->index >= reader->length;
}

bool reader_is_eof(const Reader *reader) {
    return reader_eof(reader);
}

char reader_peek(const Reader *reader) {
    return reader_eof(reader) ? '\0' : reader->src[reader->index];
}

char reader_peek_ahead(const Reader *reader, size_t offset) {
    size_t pos = reader->index + offset;
    return pos >= reader->length ? '\0' : reader->src[pos];
}

char reader_current(const Reader *reader) {
    return reader_peek(reader);
}

char reader_next(Reader *reader) {
    if (reader_eof(reader)) return '\0';
    char c = reader->src[reader->index++];
    if (c == '\n') {
        reader->line++;
        reader->column = 1;
    } else {
        reader->column++;
    }
    return c;
}

bool reader_match(Reader *reader, char expected) {
    if (reader_peek(reader) != expected) return false;
    reader_next(reader);
    return true;
}

size_t reader_offset(const Reader *reader) {
    return reader->index;
}

int reader_line(const Reader *reader) {
    return reader->line;
}

int reader_column(const Reader *reader) {
    return reader->column;
}

bool reader_skip_whitespace(Reader *reader) {
    bool skipped = false;
    while (!reader_eof(reader)) {
        int cp = reader_peek_codepoint(reader);
        if (cp < 0) break;
        
        // Check if codepoint is whitespace
        // Note: utf8_is_delimiter includes quotes which are NOT whitespace for parsing
        bool is_whitespace = (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r');
        
        if (!is_whitespace) break;
        
        reader_next_codepoint(reader);
        skipped = true;
    }
    return skipped;
}

bool reader_skip_line_comment(Reader *reader) {
    if (reader_peek(reader) != ';') return false;
    while (!reader_eof(reader)) {
        char c = reader_next(reader);
        if (c == '\n') break;
    }
    return true;
}

bool reader_skip_block_comment(Reader *reader) {
    if (!(reader_peek(reader) == '#' && reader_peek_ahead(reader, 1) == '|')) return false;
    reader_next(reader); // '#'
    reader_next(reader); // '|'
    int depth = 1;
    while (!reader_eof(reader) && depth > 0) {
        char c = reader_next(reader);
        if (c == '#' && reader_peek(reader) == '|') {
            reader_next(reader);
            depth++;
        } else if (c == '|' && reader_peek(reader) == '#') {
            reader_next(reader);
            depth--;
        }
    }
    return true;
}

bool reader_skip_ignorable(Reader *reader) {
    bool any = false;
    while (!reader_eof(reader)) {
        if (reader_skip_whitespace(reader)) {
            any = true;
            continue;
        }
        if (reader_skip_line_comment(reader)) {
            any = true;
            continue;
        }
        if (reader_skip_block_comment(reader)) {
            any = true;
            continue;
        }
        break;
    }
    return any;
}

// UTF-8 codepoint functions
int reader_peek_codepoint(const Reader *reader) {
    if (reader_eof(reader)) return -1;
    int cp;
    const char *next = utf8codepoint(reader->src + reader->index, &cp);
    return next ? cp : -1;
}

int reader_next_codepoint(Reader *reader) {
    if (reader_eof(reader)) return -1;
    int cp;
    const char *next = utf8codepoint(reader->src + reader->index, &cp);
    if (!next) return -1;
    
    // Update line and column tracking
    size_t bytes_consumed = next - (reader->src + reader->index);
    for (size_t i = 0; i < bytes_consumed; i++) {
        char c = reader->src[reader->index + i];
        if (c == '\n') {
            reader->line++;
            reader->column = 1;
        } else {
            reader->column++;
        }
    }
    
    reader->index += bytes_consumed;
    return cp;
}

bool reader_is_delimiter(const Reader *reader) {
    int cp = reader_peek_codepoint(reader);
    return cp >= 0 && utf8_is_delimiter(cp);
}

bool reader_is_symbol_char(const Reader *reader) {
    int cp = reader_peek_codepoint(reader);
    return cp >= 0 && utf8_is_symbol_char(cp);
}

