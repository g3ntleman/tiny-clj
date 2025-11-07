#include "debug.h"
#include "object.h"
#include "strings.h"
#include "list.h"
#include "symbol.h"
#include "value.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// Print AST structure with indentation for debugging
static void print_ast_recursive(CljObject *v, int depth, char *buf, size_t buf_size, int *offset) {
    if (!v) {
        *offset += snprintf(buf + *offset, buf_size - *offset, "nil");
        return;
    }
    
    // Handle immediates
    if (IS_IMMEDIATE(v)) {
        if (IS_FIXNUM(v)) {
            *offset += snprintf(buf + *offset, buf_size - *offset, "%d", as_fixnum((CljValue)v));
        } else if (IS_FIXED(v)) {
            *offset += snprintf(buf + *offset, buf_size - *offset, "%.2f", as_fixed((CljValue)v));
        } else {
            *offset += snprintf(buf + *offset, buf_size - *offset, "#<immediate>");
        }
        return;
    }
    
    switch (v->type) {
        case CLJ_SYMBOL: {
            CljSymbol *sym = as_symbol((ID)v);
            if (sym && sym->name) {
                *offset += snprintf(buf + *offset, buf_size - *offset, "SYM:%s", sym->name);
            } else {
                *offset += snprintf(buf + *offset, buf_size - *offset, "SYM:?");
            }
            break;
        }
        
        case CLJ_LIST: {
            CljList *list = as_list((ID)v);
            *offset += snprintf(buf + *offset, buf_size - *offset, "LIST[");
            if (list) {
                CljObject *current = list->first;
                int count = 0;
                while (current && count < 10) {  // Limit to 10 elements for readability
                    if (count > 0) {
                        *offset += snprintf(buf + *offset, buf_size - *offset, " ");
                    }
                    if (depth > 3) {
                        *offset += snprintf(buf + *offset, buf_size - *offset, "...");
                        break;
                    }
                    print_ast_recursive(current, depth + 1, buf, buf_size, offset);
                    current = list->rest ? as_list((ID)list->rest)->first : NULL;
                    list = list->rest ? as_list((ID)list->rest) : NULL;
                    count++;
                }
                if (current) {
                    *offset += snprintf(buf + *offset, buf_size - *offset, " ...");
                }
            }
            *offset += snprintf(buf + *offset, buf_size - *offset, "]");
            break;
        }
        
        case CLJ_STRING: {
            CljString *str = as_clj_string(v);
            if (str && str->data) {
                size_t len = str->length < 20 ? str->length : 20;
                *offset += snprintf(buf + *offset, buf_size - *offset, "\"%.*s%s\"", 
                                   (int)len, str->data, str->length > 20 ? "..." : "");
            } else {
                *offset += snprintf(buf + *offset, buf_size - *offset, "\"\"");
            }
            break;
        }
        
        case CLJ_FUNC:
            *offset += snprintf(buf + *offset, buf_size - *offset, "#<func>");
            break;
            
        case CLJ_CLOSURE:
            *offset += snprintf(buf + *offset, buf_size - *offset, "#<closure>");
            break;
            
        case CLJ_MAP:
            *offset += snprintf(buf + *offset, buf_size - *offset, "#<map>");
            break;
            
        case CLJ_VECTOR:
            *offset += snprintf(buf + *offset, buf_size - *offset, "#<vector>");
            break;
            
        default:
            *offset += snprintf(buf + *offset, buf_size - *offset, "#<type:%d>", v->type);
            break;
    }
}

// Print AST structure for debugging
const char* print_ast(CljObject *v) {
    char *buf = ALLOC(char, 4096);
    if (!buf) return strdup("#<error: out of memory>");
    
    int offset = 0;
    print_ast_recursive(v, 0, buf, 4096, &offset);
    buf[offset] = '\0';
    
    return buf;
}

