/*
 * AST Canonicalization
 *
 * Converts CljSymbolToken names to CljSymbol objects and transforms
 * CljList structures to ASTNode structures for caching.
 *
 * This step happens after parsing but before evaluation, ensuring:
 * - Symbols are interned only once (not in hot path)
 * - Memory optimization (strings are freed after conversion)
 * - AST nodes enable caching for better performance
 */

#include "ast_canon.h"
#include "common.h"
#include "object.h"
#include "symbol.h"
#include "strings.h"
#include "list.h"
#include "vector.h"
#include "map.h"
#include "namespace.h"
#include "memory.h"
#include "ast.h"
#include "parser.h"  // For resolve_alias_in_namespace
#include "meta.h"    // For meta_get and meta_set
#include "symbol_token.h"
#include <string.h>
#include <stdlib.h>

static ID canonicalize_expr(ID expr, EvalState *st);

#ifdef ENABLE_META
static void canonicalize_metadata_for_object(CljObject *obj, EvalState *st) {
    if (!obj || !st) {
        return;
    }
    ID meta = meta_get(obj);
    if (!meta) {
        return;
    }
    ID canon_meta = canonicalize_expr(meta, st);
    if (canon_meta != meta) {
        meta_set(obj, (CljObject*)canon_meta);
    }
}
#else
static inline void canonicalize_metadata_for_object(CljObject *obj, EvalState *st) {
    (void)obj;
    (void)st;
}
#endif

/**
 * @brief Canonicalize a symbol token to CljSymbol
 * @param token Symbol token containing name (may be qualified like "foo/bar")
 * @param st Evaluation state for namespace resolution
 * @return Canonicalized CljSymbol or NULL on error
 */
static CljSymbol* canonicalize_symbol_token(CljSymbolToken *token, EvalState *st) {
    CLJ_ASSERT(token != NULL);
    
    const char *name = symbol_token_data(token);
    if (name[0] == '\0') {
        return NULL;  // Empty string - cannot create symbol
    }
    
    // Handle keywords (start with :)
    bool is_keyword = (name[0] == ':');
    
    // Handle auto-qualified keywords (::keyword)
    bool auto_qualify = (is_keyword && name[1] == ':');
    
    // Find namespace separator
    const char *slash = strchr(name, '/');
    if (slash && slash > name && slash[1] != '\0') {
        // Qualified symbol: namespace/symbol
        size_t ns_len = slash - name;
        if (auto_qualify) {
            ns_len -= 2;  // Skip ::
        } else if (is_keyword) {
            ns_len -= 1;  // Skip :
        }
        
        char ns_buf[SYMBOL_NAME_MAX_LEN];
        char sym_buf[SYMBOL_NAME_MAX_LEN];
        
        if (ns_len >= SYMBOL_NAME_MAX_LEN || (strlen(slash + 1) >= SYMBOL_NAME_MAX_LEN)) {
            return NULL;  // Name too long
        }
        
        // Extract namespace part
        if (auto_qualify) {
            memcpy(ns_buf, name + 2, ns_len);
        } else if (is_keyword) {
            memcpy(ns_buf, name + 1, ns_len);
        } else {
            memcpy(ns_buf, name, ns_len);
        }
        ns_buf[ns_len] = '\0';
        
        // Extract symbol part
        const char *sym_start = slash + 1;
        size_t sym_len = strlen(sym_start);
        
        // Validate: both namespace and symbol parts must be non-empty
        if (ns_len == 0 || sym_len == 0 || sym_len >= SYMBOL_NAME_MAX_LEN) {
            return NULL;  // Invalid qualified symbol
        }
        
        memcpy(sym_buf, sym_start, sym_len);
        sym_buf[sym_len] = '\0';
        
        // Resolve namespace alias if needed
        CljSymbol *ns_name_sym = NULL;
        if (st && st->current_ns && ns_buf[0] != '\0') {
            CljSymbol *alias_resolved = resolve_alias_in_namespace(st, ns_buf);
            if (alias_resolved) {
                ns_name_sym = alias_resolved;
            }
        }
        
        if (!ns_name_sym && ns_buf[0] != '\0') {
            ns_name_sym = intern_symbol_global(ns_buf);
        }
        
        if (!ns_name_sym || sym_buf[0] == '\0') {
            return NULL;  // Invalid symbol
        }
        
        if (is_keyword) {
            // For keywords, add ':' prefix to symbol name
            char keyword_with_colon[SYMBOL_NAME_MAX_LEN];
            snprintf(keyword_with_colon, sizeof(keyword_with_colon), ":%s", sym_buf);
            return intern_symbol(ns_name_sym, keyword_with_colon);
        } else {
            return intern_symbol(ns_name_sym, sym_buf);
        }
    } else if (auto_qualify) {
        // ::keyword - auto-qualify with current namespace
        const char *keyword_name = name + 2;  // Skip ::
        if (keyword_name[0] != '\0' && st && st->current_ns && st->current_ns->name) {
            CljSymbol *ns_name_sym = st->current_ns->name;
            char keyword_with_colon[SYMBOL_NAME_MAX_LEN];
            snprintf(keyword_with_colon, sizeof(keyword_with_colon), ":%s", keyword_name);
            return intern_symbol(ns_name_sym, keyword_with_colon);
        }
        // Fall through to unqualified keyword
    }
    
    // Unqualified symbol - intern globally
    // Validate: name must not be empty (after removing : prefix for keywords)
    if (is_keyword) {
        if (name[0] == ':' && name[1] != '\0') {
            // Keyword already has ':' prefix
            return intern_symbol_global(name);
        }
        return NULL;  // Invalid keyword (just ":")
    } else {
        if (name[0] != '\0') {
            return intern_symbol_global(name);
        }
        return NULL;  // Empty symbol name
    }
}

/**
 * @brief Canonicalize an AST node (recursive)
 * @param expr Expression to canonicalize (may be CljString, CljList, etc.)
 * @param st Evaluation state
 * @return Canonicalized expression (CljSymbol for strings, ASTNode for lists) or original if unchanged
 */
static ID canonicalize_expr(ID expr, EvalState *st) {
    CLJ_ASSERT(st != NULL);
    
    if (!expr) {
        return NULL;
    }
    
    if (IS_IMMEDIATE(expr)) {
        return expr;  // Immediate values don't need canonicalization
    }

    // NOTE: Metadata canonicalization is done for specific types below (lists, vectors, maps)
    // to avoid infinite recursion when metadata objects have their own metadata.
    
    unsigned char tag = TAG(expr);
    
    // Convert symbol tokens (stored as special strings) to CljSymbol
    if (tag == CLJ_SYMBOL_TOKEN) {
        CljSymbolToken *token = (CljSymbolToken*)expr;
        CljSymbol *sym = canonicalize_symbol_token(token, st);
        if (sym) {
            RELEASE(token);  // Free the token after conversion
            return sym;
        }
        return expr;  // Conversion failed (invalid symbol) - leave as token
    }

    if (tag == CLJ_STRING) {
        return expr;  // Actual string literal, leave unchanged
    }
    
    // Convert CljList to ASTNode (for function calls and variable access)
    if (list_type_matches(tag)) {
        CljList *list = as_list(expr);
        CLJ_ASSERT(list != NULL);
        
        ID first = canonicalize_expr(list->first, st);
        ID rest = NULL;
        
        if (list->rest) {
            rest = canonicalize_expr(list->rest, st);
        }
        
        // Create ASTNode instead of CljList
        CljASTNode *node = make_ast_node(first, rest);
        if (!node) {
            return expr;  // Out of memory - return original
        }
        
        // Copy metadata if present (from CljList or ASTNode)
        // Metadata is stored in global registry using object pointers as keys
        // We need to copy metadata from the old list pointer to the new ASTNode pointer
#ifdef ENABLE_META
        ID meta = meta_get((CljObject*)expr);
        if (meta) {
            ID canon_meta = canonicalize_expr(meta, st);
            meta_set((CljObject*)node, (CljObject*)canon_meta);
        }
#endif
        
        RELEASE(list);  // Free the original list
        return node;
    }
    
    // For other types (vectors, maps), recursively canonicalize elements
    if (tag == CLJ_VECTOR) {
        CljVector *vec = (CljVector*)expr;
        CLJ_ASSERT(vec != NULL);
        int count = vector_count(vec);
        bool changed = false;
        ID *canon_elems = (ID*)malloc(count * sizeof(ID));
        CLJ_ASSERT(canon_elems != NULL && "Out of memory");
        
        for (int i = 0; i < count; i++) {
            ID elem = vector_nth(vec, i);
            canon_elems[i] = canonicalize_expr(elem, st);
            if (canon_elems[i] != elem) {
                changed = true;
            }
        }
        
        if (changed) {
            // Create new vector with canonicalized elements
            CljVector *new_vec = make_vector(count, CLJ_VECTOR);
            for (int i = 0; i < count; i++) {
                new_vec = vector_conj(new_vec, canon_elems[i]);
            }
#ifdef ENABLE_META
            ID meta = meta_get((CljObject*)vec);
            if (meta) {
                ID canon_meta = canonicalize_expr(meta, st);
                meta_set((CljObject*)new_vec, (CljObject*)canon_meta);
            }
#endif
            free(canon_elems);
            RELEASE(vec);  // Free original vector
            return new_vec;
        }
        
        free(canon_elems);
        return expr;  // No changes needed
    }
    
    if (tag == CLJ_MAP) {
        CljMap *map = (CljMap*)expr;
        CLJ_ASSERT(map != NULL);
        CljMap *new_map = NULL;
        bool changed = false;
        
        // Canonicalize keys and values
        MAP_FOR_EACH(map, key, value) {
            ID canon_key = canonicalize_expr(key, st);
            ID canon_value = canonicalize_expr(value, st);
            if (canon_key != key || canon_value != value) {
                if (!new_map) {
                    new_map = make_map(map_count(map));
                }
                new_map = map_assoc(new_map, canon_key, canon_value);
                changed = true;
            } else if (new_map) {
                new_map = map_assoc(new_map, key, value);
            }
        }
        
        if (changed && new_map) {
#ifdef ENABLE_META
            ID meta = meta_get((CljObject*)map);
            if (meta) {
                ID canon_meta = canonicalize_expr(meta, st);
                meta_set((CljObject*)new_map, (CljObject*)canon_meta);
            }
#endif
            RELEASE(map);  // Free original map
            return new_map;
        }
        
        return expr;  // No changes needed
    }
    
    // Other types don't need canonicalization
    return expr;
}

/**
 * @brief Canonicalize a parsed expression
 * @param parsed_expr Parsed expression (may contain CljString symbols)
 * @param st Evaluation state
 * @return Canonicalized expression with CljSymbol objects and ASTNodes
 */
ID canonicalize_ast(ID parsed_expr, EvalState *st) {
    CLJ_ASSERT(st != NULL);
    return canonicalize_expr(parsed_expr, st);
}


