#include "debug.h"
#include "object.h"
#include "strings.h"
#include "list.h"
#include "symbol.h"
#include "value.h"
#include "memory.h"
#include "types.h"
#include "mini_format.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#ifdef DEBUG
// Print AST as Clojure code for debugging
static void print_ast_recursive(ID v, int depth, char *buf, size_t buf_size, int *offset) {
    if (!v) {
        *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "nil");
        return;
    }

    // Handle immediates
    if (IS_IMMEDIATE(v)) {
        switch (TAG(v)) {
            case CLJ_FIXNUM:
                *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "%d", as_fixnum((CljValue)v));
                break;
            case CLJ_FLOAT:
                *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "%.2f", as_fixed((CljValue)v));
                break;
            default:
                *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "#<immediate>");
                break;
        }
        return;
    }

    CljObject *obj = (CljObject*)v;
    switch (obj->type) {
        case CLJ_SYMBOL: {
            CljSymbol *sym = as_symbol(v);
            // Check if this is SYM_NIL (nil symbol) - distinguish from evaluated nil (NULL)
            if (v == SYM_NIL) {
                *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "SYM:nil");
            } else if (sym && sym->cname) {
                // Output symbol name with SYM: prefix to distinguish from evaluated values
                // If symbol has namespace, output as "SYM:namespace/name"
                if (sym->ns_name && sym->ns_name->cname) {
                    *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "SYM:%s/%s",
                                       sym->ns_name->cname, sym->cname);
                } else {
                    *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "SYM:%s", sym->cname);
                }
            } else {
                *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "SYM:?");
            }
            break;
        }

        case CLJ_LIST: {
            CljList *list = as_list(v);
            // Output list with type indicator: [List: (element1 element2 ...)]
            *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "[List: (");
            if (list) {
                CljObject *current = list->first;
                int count = 0;
                bool first = true;
                while (current && count < 20) {  // Limit to 20 elements for readability
                    if (!first) {
                        *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, " ");
                    }
                    first = false;
                    if (depth > 5) {
                        *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "...");
                        break;
                    }
                    print_ast_recursive(current, depth + 1, buf, buf_size, offset);
                    current = list->rest ? as_list(list->rest)->first : NULL;
                    list = list->rest ? as_list(list->rest) : NULL;
                    count++;
                }
                if (current) {
                    *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, " ...");
                }
            }
            *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, ")]");
            break;
        }

        case CLJ_STRING: {
            CljString *str = as_clj_string(v);
            if (str && string_length((ID)str) > 0) {
                size_t full_len = string_length((ID)str);
                size_t len = full_len < 20 ? full_len : 20;
                const char *data = string_data((ID)str);
                *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "\"%.*s%s\"",
                                   (int)len, data, full_len > 20 ? "..." : "");
            } else {
                *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "\"\"");
            }
            break;
        }

        case CLJ_FUNC:
            *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "#<func>");
            break;

        case CLJ_CLOSURE:
            *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "#<closure>");
            break;

        case CLJ_MAP_PERSISTENT:
            *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "#<map>");
            break;

        case CLJ_VECTOR_PERSISTENT:case CLJ_VECTOR_TRANSIENT:
            *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "#<vector>");
            break;

        case CLJ_AST_NODE: {
            CljASTNode *node = (CljASTNode*)v;
            // Output ASTNode with type indicator: [ASTNode: (element1 element2 ...)]
            *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "[ASTNode: (");
            if (node) {
                CljObject *current = node->first;
                int count = 0;
                bool first = true;
                CljList *rest = node->rest ? as_list(node->rest) : NULL;
                while (current && count < 20) {  // Limit to 20 elements for readability
                    if (!first) {
                        *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, " ");
                    }
                    first = false;
                    if (depth > 5) {
                        *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "...");
                        break;
                    }
                    print_ast_recursive(current, depth + 1, buf, buf_size, offset);
                    current = rest ? rest->first : NULL;
                    rest = rest && rest->rest ? as_list(rest->rest) : NULL;
                    count++;
                }
                if (current) {
                    *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, " ...");
                }
            }
            *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, ")]");
            break;
        }
        default:
            *offset += mini_snprintf(buf + *offset, buf_size - (size_t)*offset, "#<type:%d>", obj->type);
            break;
    }
}

// Print AST structure for debugging
const char* print_ast(ID v) {
    char *buf = ALLOC(char, 4096);

    int offset = 0;
    print_ast_recursive(v, 0, buf, 4096, &offset);
    buf[offset] = '\0';

    return buf;
}
#endif // DEBUG

/**
 * @brief Check if an object is a zombie (already freed)
 * @param o Object to check (can be NULL or immediate)
 * @return true if object is a zombie, false otherwise
 */
bool is_zombie(ID o) {
    if (!o || IS_IMMEDIATE(o)) {
        return false;  // NULL and immediates cannot be zombies
    }

    CljObject *obj = (CljObject*)o;
    return obj->rc == 0;
}
