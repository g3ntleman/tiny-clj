#ifndef SUBJECTIVE_C_SYMBOL_TOKEN_H
#define SUBJECTIVE_C_SYMBOL_TOKEN_H

#include "object.h"
#include <stdint.h>

typedef struct CljSymbolToken {
    CljObject base;
    uint16_t length;
    char data[];
} CljSymbolToken;

CljSymbolToken* make_symbol_token(const char *str);

static inline bool is_symbol_token(ID obj) {
    return obj && TAG(obj) == CLJ_SYMBOL_TOKEN;
}

static inline const char* symbol_token_data(const CljSymbolToken *token) {
    return token ? token->data : "";
}

static inline uint16_t symbol_token_length(const CljSymbolToken *token) {
    return token ? token->length : 0;
}

#endif // SUBJECTIVE_C_SYMBOL_TOKEN_H

