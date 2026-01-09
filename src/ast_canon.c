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
#include "eval.h"    // For eval_function_call
#include "macro.h"   // For lookup_macro_resolve
// is_special_symbol is in symbol.h (already included)
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ============================================================================
// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Move metadata from source to destination (clears source entry to prevent leaks)
static INLINE void move_meta(ID src, ID dst) {
#if defined(META_ENABLED) && META_ENABLED
    ID meta = meta_get(src);
    if (meta) {
        meta_set(dst, meta);
        meta_clear(src);
    }
#else
    (void)src; (void)dst;
#endif
}

static ID canonicalize_expr(ID expr, EvalState *st, bool in_quote);

// Canonicalize a list tail (rest chain) into plain CLJ_LIST cons cells.
// Important: This must NOT run macro expansion / destructuring on the tail itself.
// It only canonicalizes each element (which may itself be a list expression).
static CljList* canonicalize_rest_to_plain_list(ID rest_expr, EvalState *st, bool in_quote) {
    if (!rest_expr) return NULL;
    if (!list_type_matches(TAG(rest_expr))) return NULL;

    CljList *src = as_list(rest_expr);
    ID first = canonicalize_expr(src->first, st, in_quote);
    CljList *rest = src->rest ? canonicalize_rest_to_plain_list(src->rest, st, in_quote) : NULL;

    // Fast path: already a plain list and nothing changed.
    if (TAG(rest_expr) == CLJ_LIST && first == src->first && (ID)rest == src->rest) {
        return src;
    }

    ID node = AUTORELEASE(make_list(first, rest));
    if (!node) {
        return src;  // OOM: fall back to original
    }
    move_meta(rest_expr, node);
    return as_list(node);
}

// ============================================================================
// DESTRUCTURING SUPPORT (compile-time transformation)
// Uses Clojure's destructure function when available
// ============================================================================

// Check if vector elements need destructuring (any element is not a simple symbol)
// pairs_only: for bindings [name1 val1 name2 val2 ...] check only names (even indices)
static bool vector_needs_destructuring(CljVector *vec, bool pairs_only) {
    int i = 0;
    VECTOR_FOR_EACH(vec, elem) {
        if ((!pairs_only || (i++ & 1) == 0) && TAG(elem) != CLJ_SYMBOL) return true;
    }
    return false;
}

#define bindings_need_destructuring(v) vector_needs_destructuring(v, true)
#define params_need_destructuring(v)   vector_needs_destructuring(v, false)

// Cached destructure function (resolved once after bootstrap)
static ID destructure_fn = NULL;

// Call Clojure (destructure bindings), returns NULL if not available (bootstrap)
static CljVector* destructure(EvalState *st, CljVector *bindings) {
    if (!destructure_fn) {
        ID resolved = ns_resolve(st, SYM_DESTRUCTURE);
        if (resolved == NOT_FOUND || !resolved) return NULL;  // Bootstrap: not loaded yet
        destructure_fn = resolved;
    }
    
    ID args[] = { bindings };
    ID result = eval_function_call(destructure_fn, args, 1, NULL, st);
    return (TAG(result) == CLJ_VECTOR) ? as_vector(result) : NULL;
}

// Gensym counter for fn/defn/loop param destructuring
static unsigned long param_gensym_counter = 0;

// Transform params with destructuring, returns new_params and let_bindings
// Returns NULL for let_bindings if no destructuring needed
static CljVector* transform_params(EvalState *st, CljVector *params, CljVector **out_let_bindings) {
    unsigned int count = vector_count(params);
    CljVector *new_params = make_vector(count, CLJ_VECTOR);
    CljVector *let_bindings = make_vector(count * 2, CLJ_VECTOR);
    bool has_destructuring = false;
    
    VECTOR_FOR_EACH(params, param) {
        unsigned char tag = TAG(param);
        if (tag == CLJ_VECTOR || tag == CLJ_MAP) {
            char name[64];
            snprintf(name, sizeof(name), "p__%lu", ++param_gensym_counter);
            CljSymbol *gsym = intern_symbol_global(name);
            ASSIGN(new_params, vector_conj(new_params, gsym));
            ASSIGN(let_bindings, vector_conj(let_bindings, param));
            ASSIGN(let_bindings, vector_conj(let_bindings, gsym));
            has_destructuring = true;
        } else {
            ASSIGN(new_params, vector_conj(new_params, param));
        }
    }
    
    if (has_destructuring && vector_count(let_bindings) > 0) {
        *out_let_bindings = destructure(st, let_bindings);
    } else {
        *out_let_bindings = NULL;
    }
    return new_params;
}

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
 * @param in_quote If true, lists stay as CljList (not converted to ASTNode)
 * @return Canonicalized expression (CljSymbol for strings, ASTNode for lists) or original if unchanged
 */
static ID canonicalize_expr(ID expr, EvalState *st, bool in_quote) {
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
    
    // Convert symbol tokens (stored as special strings) to CljSymbol - always, even in quote
    if (tag == CLJ_SYMBOL_TOKEN) {
        CljSymbolToken *token = (CljSymbolToken*)expr;
        CljSymbol *sym = canonicalize_symbol_token(token, st);
        if (sym) {
            return sym;
        }
        return expr;  // Conversion failed (invalid symbol) - leave as token
    }

    if (tag == CLJ_STRING) {
        return expr;  // Actual string literal, leave unchanged
    }
    
    // Handle lists and ASTNodes: Convert to ASTNode unless in_quote
    // NOTE: Parser produces ASTNodes, but we handle both for robustness
    if (list_type_matches(tag)) {
        CljList *list = as_list(expr);
        CLJ_ASSERT(list != NULL);
        
        ID first = canonicalize_expr(list->first, st, in_quote);
        
        // Check if we're entering a quote expression
        // Compare against SYM_QUOTE to detect (quote ...) forms
        bool is_quote_form = (first == SYM_QUOTE);
        bool child_in_quote = in_quote || is_quote_form;
        
        // ========== MACRO EXPANSION (compile-time) ==========
        // Expand macros before destructuring and other transformations
        // Skip macro lookup for special forms and native functions (they can't be macros)
        if (!in_quote && first && TAG(first) == CLJ_SYMBOL) {
            CljSymbol *head_sym = as_symbol(first);
            CljFunction *macro = NULL;
            if (!is_special_symbol(head_sym) && !is_native_symbol(head_sym)) {
                macro = lookup_macro_resolve(st, head_sym);
            }
            if (macro) {
                // Collect unevaluated arguments for macro call
                // NOTE: Arguments are NOT canonicalized here - macros work with raw forms
                // The expanded form will be canonicalized recursively below
                ID args[16];
                int argc = 0;
                for (CljList *cur = list->rest ? as_list(list->rest) : NULL;
                     cur && argc < 16;
                     cur = cur->rest ? as_list(cur->rest) : NULL) {
                    args[argc++] = cur->first;
                }
                
                // Call macro function to expand the form
                ID expanded = eval_function_call((CljObject*)macro, args, argc, NULL, st);
                if (!expanded) return NULL;
                
                // Transfer metadata from original form to expanded form
                move_meta(list, expanded);
                
                // CRITICAL: Check if expanded is an immediate value (e.g., (-> 5) returns 5)
                // Immediate values don't need wrapping and should be returned directly
                if (IS_IMMEDIATE(expanded)) {
                    // Return immediate value directly (canonicalize_expr handles this)
                    return canonicalize_expr(expanded, st, in_quote);
                }
                
                // CRITICAL: Ensure expanded form is a list-like type (CLJ_LIST or CLJ_AST_NODE)
                // Macros can return PersistentList (CLJ_LIST) which needs to be canonicalized
                unsigned char expanded_tag = TAG(expanded);
                if (!list_type_matches(expanded_tag)) {
                    // Expanded form is not a list - this shouldn't happen for threading macros
                    // but handle it gracefully by wrapping in a list
                    expanded = AUTORELEASE(make_list(expanded, NULL));
                }
                
                // Recursively canonicalize the expanded form
                // This will convert CLJ_LIST to CLJ_AST_NODE and canonicalize all elements
                return canonicalize_expr(expanded, st, in_quote);
            }
        }
        
        // ========== DESTRUCTURING TRANSFORMATION (compile-time) ==========
        // Transform let/loop bindings and fn/defn params
        if (!in_quote && first && TAG(first) == CLJ_SYMBOL) {
            CljSymbol *head_sym = as_symbol(first);

            // (let [bindings] body...) - expand bindings using Clojure destructure
            if (head_sym == SYM_LET) {
                CljList *rest1 = list->rest ? as_list(list->rest) : NULL;
                if (rest1 && rest1->first && TAG(rest1->first) == CLJ_VECTOR) {
                    CljVector *bindings = as_vector(rest1->first);
                    if (bindings_need_destructuring(bindings)) {
                        CljVector *expanded = destructure(st, bindings);
                        if (expanded) {
                            // Rebuild: (let expanded-bindings body...)
                            ID new_rest = AUTORELEASE(make_list(expanded, rest1->rest ? as_list(rest1->rest) : NULL));
                            ID new_list = AUTORELEASE(make_list(first, as_list(new_rest)));
                            return canonicalize_expr(new_list, st, in_quote);
                        }
                        // destructure not available (bootstrap) - skip transformation
                    }
                }
            }
            
            // (loop [bindings] body...) - special handling for recur compatibility
            // Transform: (loop [[a b] init, sum 0] body)
            //       To: (loop [loop__1 init, sum 0] (let [expanded-bindings] body))
            if (head_sym == SYM_LOOP) {
                CljList *rest1 = list->rest ? as_list(list->rest) : NULL;
                if (rest1 && rest1->first && TAG(rest1->first) == CLJ_VECTOR) {
                    CljVector *bindings = as_vector(rest1->first);
                    if (bindings_need_destructuring(bindings)) {
                        static unsigned long gensym_counter = 0;
                        unsigned int count = vector_count(bindings);
                        CljVector *loop_bindings = make_vector(count, CLJ_VECTOR);
                        CljVector *let_bindings = make_vector(count, CLJ_VECTOR);
                        
                        // Process each binding pair
                        for (unsigned int i = 0; i < count; i += 2) {
                            ID binding_form = vector_nth(bindings, i);
                            ID init_expr = vector_nth(bindings, i + 1);
                            
                            if (TAG(binding_form) != CLJ_SYMBOL) {
                                // Destructuring binding - create gensym
                                char name[64];
                                snprintf(name, sizeof(name), "loop__%lu", ++gensym_counter);
                                CljSymbol *gsym = intern_symbol_global(name);
                                ASSIGN(loop_bindings, vector_conj(loop_bindings, gsym));
                                ASSIGN(loop_bindings, vector_conj(loop_bindings, init_expr));
                                ASSIGN(let_bindings, vector_conj(let_bindings, binding_form));
                                ASSIGN(let_bindings, vector_conj(let_bindings, gsym));
                            } else {
                                // Simple symbol binding - keep as-is
                                ASSIGN(loop_bindings, vector_conj(loop_bindings, binding_form));
                                ASSIGN(loop_bindings, vector_conj(loop_bindings, init_expr));
                            }
                        }
                        
                        // Expand let_bindings using Clojure destructure
                        CljVector *expanded_let_bindings = NULL;
                        if (vector_count(let_bindings) > 0) {
                            expanded_let_bindings = destructure(st, let_bindings);
                            if (!expanded_let_bindings) {
                                // Bootstrap - skip transformation
                                // (expanded_let_bindings stays NULL, will be checked below)
                            }
                        }
                        
                        // Get body expressions
                        CljList *body_list = rest1->rest ? as_list(rest1->rest) : NULL;
                        ID body = body_list ? body_list->first : NULL;
                        
                        // Wrap body in let if there are destructuring bindings
                        ID new_body = body;
                        if (expanded_let_bindings && vector_count(expanded_let_bindings) > 0) {
                            new_body = make_list(SYM_LET,
                                                 make_list(expanded_let_bindings,
                                                           make_list(body, NULL)));
                        }
                        
                        // Rebuild: (loop loop-bindings new-body)
                        ID loop_form = AUTORELEASE(make_list(first,
                                                            make_list(loop_bindings,
                                                                      make_list(new_body, NULL))));
                        return canonicalize_expr(loop_form, st, in_quote);
                    }
                }
            }
            
            // (fn [params] body) or (fn name [params] body)
            if (head_sym == SYM_FN) {
                CljList *rest1 = list->rest ? as_list(list->rest) : NULL;
                if (rest1) {
                    ID second = rest1->first;
                    CljVector *params = NULL;
                    CljList *body_rest = NULL;
                    bool named = false;
                    
                    if (second && TAG(second) == CLJ_SYMBOL && !IS_KEYWORD(second)) {
                        // Named fn: (fn name [params] body)
                        named = true;
                        CljList *rest2 = rest1->rest ? as_list(rest1->rest) : NULL;
                        if (rest2 && rest2->first && TAG(rest2->first) == CLJ_VECTOR) {
                            params = as_vector(rest2->first);
                            body_rest = rest2->rest ? as_list(rest2->rest) : NULL;
                        }
                    } else if (second && TAG(second) == CLJ_VECTOR) {
                        // Anonymous fn: (fn [params] body)
                        params = as_vector(second);
                        body_rest = rest1->rest ? as_list(rest1->rest) : NULL;
                    }
                    
                    if (params && params_need_destructuring(params)) {
                        CljVector *expanded_let_bindings = NULL;
                        CljVector *new_params = transform_params(st, params, &expanded_let_bindings);
                        
                        if (expanded_let_bindings && vector_count(expanded_let_bindings) > 0) {
                            ID body = body_rest ? body_rest->first : NULL;
                            CljList *let_form = make_list(SYM_LET,
                                                          make_list(expanded_let_bindings,
                                                                    make_list(body, NULL)));
                            ID new_form = named
                                ? (ID)make_list(first, make_list(second, make_list(new_params, make_list(let_form, NULL))))
                                : (ID)make_list(first, make_list(new_params, make_list(let_form, NULL)));
                            return canonicalize_expr(AUTORELEASE(new_form), st, in_quote);
                        }
                    }
                }
            }
            // Note: defn is a macro that expands to (def name (fn ...)),
            // so fn's destructuring handler above covers defn as well
        }
        // ========== END DESTRUCTURING TRANSFORMATION ==========
        
        // Canonicalize rest of list into plain CLJ_LIST cons cells.
        // This ensures only the callsite head becomes CLJ_AST_NODE.
        CljList *rest_list = list->rest ? canonicalize_rest_to_plain_list(list->rest, st, child_in_quote) : NULL;

        // Early exit if nothing changed AND representation is already correct.
        // - in_quote: entire list must be CLJ_LIST
        // - normal: head must be CLJ_AST_NODE, tail should be CLJ_LIST
        bool correct_type = in_quote ? (tag == CLJ_LIST) : (tag == CLJ_AST_NODE);
        bool tail_is_plain_list = (!list->rest) || (TAG(list->rest) == CLJ_LIST);
        if (correct_type && tail_is_plain_list && first == list->first && (ID)rest_list == list->rest) {
            return expr;
        }

        // Create appropriate container based on context
        ID result = in_quote
            ? AUTORELEASE(make_list(first, rest_list))
            : AUTORELEASE(make_ast_node(first, (CljObject*)rest_list));
        
        if (!result) {
            return expr;  // Out of memory - return original
        }
        
        // Copy metadata (no recursive canonicalization - metadata has no symbol tokens)
        move_meta(expr, result);
        return result;
    }
    
    // For other types (vectors, maps), recursively canonicalize elements
    if (tag == CLJ_VECTOR) {
        CljVector *vec = (CljVector*)expr;
        CLJ_ASSERT(vec != NULL);
        int count = vector_count(vec);
        
        // Stack buffer for small vectors (avoid malloc)
        ID stack_buf[16];
        ID *canon_elems = (count <= 16) ? stack_buf : (ID*)malloc(count * sizeof(ID));
        CLJ_ASSERT(canon_elems != NULL && "Out of memory");
        
        bool changed = false;
        int i = 0;
        VECTOR_FOR_EACH(vec, elem) {
            canon_elems[i] = canonicalize_expr(elem, st, in_quote);
            if (canon_elems[i] != elem) {
                changed = true;
            }
            i++;
        }
        
        if (!changed) {
            if (count > 16) free(canon_elems);
            return expr;  // No changes needed
        }
        
        // Create new vector with canonicalized elements
        CljVector *new_vec = make_vector(count, CLJ_VECTOR);
        for (int i = 0; i < count; i++) {
            ASSIGN(new_vec, vector_conj(new_vec, canon_elems[i]));
        }
        move_meta(vec, new_vec);
        
        if (count > 16) free(canon_elems);
        return AUTORELEASE(new_vec);
    }
    
    if (tag == CLJ_MAP) {
        CljMap *map = (CljMap*)expr;
        CLJ_ASSERT(map != NULL);
        CljMap *new_map = NULL;
        bool changed = false;
        
        // Canonicalize keys and values
        MAP_FOR_EACH(map, key, value) {
            ID canon_key = canonicalize_expr(key, st, in_quote);
            ID canon_value = canonicalize_expr(value, st, in_quote);
            if (canon_key != key || canon_value != value) {
                if (!new_map) {
                    new_map = make_map(map_count(map));
                }
                ASSIGN(new_map, map_assoc(new_map, canon_key, canon_value));
                changed = true;
            } else if (new_map) {
                ASSIGN(new_map, map_assoc(new_map, key, value));
            }
        }
        
        if (changed && new_map) {
            move_meta(map, new_map);
            return AUTORELEASE(new_map);
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
    ID result = NULL;
    bool needs_escape = false;
    int rc_after_retain = 0;

    // Canonicalization can allocate a lot of temporary AUTORELEASE containers.
    // Scope that churn to a nested pool so it doesn't inflate the caller's pool.
    // If the pool implementation ever becomes draining again, retain the result
    // inside the pool so it survives pop, then re-autorelease it into the
    // caller's pool.
    WITH_AUTORELEASE_POOL({
        result = canonicalize_expr(parsed_expr, st, false);

        if (result && !IS_IMMEDIATE(result) && result != parsed_expr) {
            unsigned char tag = TAG(result);
            if (list_type_matches(tag) || tag == CLJ_VECTOR || tag == CLJ_MAP) {
                needs_escape = true;
                RETAIN(result);
                rc_after_retain = retain_count(result);
            }
        }
    });

    if (needs_escape) {
        // Current implementation uses weak pool semantics (pop doesn't release).
        // If the pool didn't drain, balance our retain to avoid leaking.
        int rc_after_pop = retain_count(result);
        if (rc_after_pop == rc_after_retain) {
            RELEASE(result);
        }

        // Only re-autorelease if there is an outer pool.
        if (is_autorelease_pool_active()) {
            AUTORELEASE(result);
        }
    }

    return result;
}


