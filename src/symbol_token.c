#include "symbol_token.h"
#include "memory.h"
#include "common.h"
#include <string.h>
#include <assert.h>

/** Create symbol token with source position; rc=1, caller releases. */
CljSymbolToken* make_symbol_token_with_loc(const char *str, uint16_t line, uint16_t col) {
    CLJ_ASSERT(str != NULL);

    const char *source = (str && str[0] != '\0') ? str : "";
    size_t len = strlen(source);
    assert(len <= UINT16_MAX && "Symbol token length exceeds 16-bit limit (65,535 chars)");

    CljSymbolToken *token = (CljSymbolToken*)alloc(sizeof(CljSymbolToken) + len + 1, 1, CLJ_SYMBOL_TOKEN);
    token->base.type = CLJ_SYMBOL_TOKEN;
    token->length = (uint16_t)len;
    token->line = line;
    token->col = col;
    memcpy(token->data, source, len + 1);

    return token;
}

/** Create symbol token without location (line/col = 0). */
CljSymbolToken* make_symbol_token(const char *str) {
    return make_symbol_token_with_loc(str, 0, 0);  // No location info
}
