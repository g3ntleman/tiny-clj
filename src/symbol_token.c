#include "symbol_token.h"
#include "memory.h"
#include "common.h"
#include <string.h>
#include <assert.h>

CljSymbolToken* make_symbol_token(const char *str) {
    CLJ_ASSERT(str != NULL);

    const char *source = (str && str[0] != '\0') ? str : "";
    size_t len = strlen(source);
    assert(len <= UINT16_MAX && "Symbol token length exceeds 16-bit limit (65,535 chars)");

    CljSymbolToken *token = (CljSymbolToken*)alloc(sizeof(CljSymbolToken) + len + 1, 1, CLJ_SYMBOL_TOKEN);
    token->base.type = CLJ_SYMBOL_TOKEN;
    token->base.rc = 1;
    token->length = (uint16_t)len;
    memcpy(token->data, source, len + 1);

    return token;
}

