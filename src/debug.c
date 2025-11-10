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

// Print AST as Clojure code for debugging
static void print_ast_recursive(CljObject *v, int depth, char *buf, size_t buf_size, int *offset) {
    if (!v) {
        *offset += snprintf(buf + *offset, buf_size - *offset, "nil");
        return;
    }
    
    // Handle immediates
    if (IS_IMMEDIATE(v)) {
        switch (TAG(v)) {
            case CLJ_INT:
                *offset += snprintf(buf + *offset, buf_size - *offset, "%d", as_fixnum((CljValue)v));
                break;
            case CLJ_FLOAT:
                *offset += snprintf(buf + *offset, buf_size - *offset, "%.2f", as_fixed((CljValue)v));
                break;
            default:
                *offset += snprintf(buf + *offset, buf_size - *offset, "#<immediate>");
                break;
        }
        return;
    }
    
    switch (v->type) {
        case CLJ_SYMBOL: {
            CljSymbol *sym = as_symbol((ID)v);
            if (sym && sym->name) {
                // Output symbol name as Clojure code (no "SYM:" prefix)
                *offset += snprintf(buf + *offset, buf_size - *offset, "%s", sym->name);
            } else {
                *offset += snprintf(buf + *offset, buf_size - *offset, "?");
            }
            break;
        }
        
        case CLJ_LIST: {
            CljList *list = as_list((ID)v);
            // Output list as Clojure code: (element1 element2 ...)
            *offset += snprintf(buf + *offset, buf_size - *offset, "(");
            if (list) {
                CljObject *current = list->first;
                int count = 0;
                bool first = true;
                while (current && count < 20) {  // Limit to 20 elements for readability
                    if (!first) {
                        *offset += snprintf(buf + *offset, buf_size - *offset, " ");
                    }
                    first = false;
                    if (depth > 5) {
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
            *offset += snprintf(buf + *offset, buf_size - *offset, ")");
            break;
        }
        
        case CLJ_STRING: {
            CljString *str = as_clj_string(v);
            if (str && str->length > 0) {
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


