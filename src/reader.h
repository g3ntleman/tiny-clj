#ifndef TINY_CLJ_READER_H
#define TINY_CLJ_READER_H

#include <stddef.h>
#include <stdbool.h>
#include "utf8.h"

typedef struct {
    const char *src;
    size_t length;
    size_t index;
    int line;
    int column;
} Reader;

void reader_init(Reader *reader, const char *src);
bool reader_eof(const Reader *reader);
bool reader_is_eof(const Reader *reader);
char reader_peek(const Reader *reader);
char reader_peek_ahead(const Reader *reader, size_t offset);
char reader_current(const Reader *reader);
char reader_next(Reader *reader);
bool reader_match(Reader *reader, char expected);
size_t reader_offset(const Reader *reader);
int reader_line(const Reader *reader);
int reader_column(const Reader *reader);
bool reader_skip_whitespace(Reader *reader);
bool reader_skip_line_comment(Reader *reader);
bool reader_skip_block_comment(Reader *reader);
bool reader_skip_ignorable(Reader *reader);

// UTF-8 codepoint functions
int reader_peek_codepoint(const Reader *reader);
int reader_next_codepoint(Reader *reader);
bool reader_is_delimiter(const Reader *reader);
bool reader_is_symbol_char(const Reader *reader);

static inline char reader_peek_char(const Reader *reader) {
  return reader_peek(reader);
}

static inline void reader_advance(Reader *reader) { reader_next(reader); }

static inline void reader_skip_all(Reader *reader) {
  while (reader_skip_ignorable(reader)) {
  }
}

/** Convenience macro: create stack Reader and initialize from C-string. */
#define READER_FROM_CSTR(name, cstr)                                           \
  Reader name;                                                                 \
  reader_init(&(name), (cstr))

#endif // TINY_CLJ_READER_H

