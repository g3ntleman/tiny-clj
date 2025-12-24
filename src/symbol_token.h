#ifndef TINY_CLJ_SYMBOL_TOKEN_H
#define TINY_CLJ_SYMBOL_TOKEN_H

#include "subjective-c/object.h"
#include <stdint.h>

typedef struct CljSymbolToken {
    CljObject base;
    uint16_t length;
    uint16_t line;   // Source line (1-based), 0 = unknown
    uint16_t col;    // Source column (1-based), 0 = unknown
    char data[];
} CljSymbolToken;

CljSymbolToken* make_symbol_token(const char *str);
CljSymbolToken* make_symbol_token_with_loc(const char *str, uint16_t line, uint16_t col);

static inline bool is_symbol_token(ID obj) {
    return obj && TAG(obj) == CLJ_SYMBOL_TOKEN;
}

static inline const char* symbol_token_data(const CljSymbolToken *token) {
    return token ? token->data : "";
}

static inline uint16_t symbol_token_length(const CljSymbolToken *token) {
    return token ? token->length : 0;
}

#endif // TINY_CLJ_SYMBOL_TOKEN_H

