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
#include "debug.h"   // For print_ast
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

static ID canonicalize_expr_with_scope(ID expr, EvalState *st, bool in_quote, CljTransientVector *scope_stack);
static ID canonicalize_expr(ID expr, EvalState *st, bool in_quote);

// Canonicalize a list tail (rest chain) into plain CLJ_LIST cons cells.
// Important: This must NOT run macro expansion / destructuring on the tail itself.
// It only canonicalizes each element (which may itself be a list expression).
static CljList* canonicalize_rest_to_plain_list(ID rest_expr, EvalState *st, bool in_quote, CljTransientVector *scope_stack) {
    if (!rest_expr) return NULL;
    if (!is_list_type(TAG(rest_expr))) return NULL;

    CljList *src = as_list(rest_expr);
    ID first = canonicalize_expr_with_scope(src->first, st, in_quote, scope_stack);
    CljList *rest = src->rest ? canonicalize_rest_to_plain_list(src->rest, st, in_quote, scope_stack) : NULL;

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
static bool vector_needs_destructuring(CljPersistentVector *vec, bool pairs_only) {
    int i = 0;
    VECTOR_FOR_EACH(vec, elem) {
        if ((!pairs_only || (i++ & 1) == 0) && TAG(elem) != CLJ_SYMBOL) return true;
    }
    return false;
}

#define bindings_need_destructuring(v) vector_needs_destructuring(v, true)
#define params_need_destructuring(v)   vector_needs_destructuring(v, false)

// ============================================================================
// LEXICAL ADDRESSING (compile-time): scope stack + (depth, slot) lookup
// ============================================================================

// Scope stack: vector of vectors, stack semantics.
// Top = innermost scope (depth=0).
static inline unsigned int scope_stack_count(CljTransientVector *stack) {
    if (!stack) return 0;
    CljPersistentVector *backing = vector_persistent(stack);
    return backing ? vector_count(backing) : 0;
}

static inline CljPersistentVector* scope_stack_get_from_top(CljTransientVector *stack, unsigned int depth) {
    if (!stack) return NULL;
    CljPersistentVector *backing = vector_persistent(stack);
    unsigned int cnt = backing ? vector_count(backing) : 0;
    if (cnt == 0 || depth >= cnt) return NULL;
    ID scope_id = vector_nth(backing, (cnt - 1) - depth);
    if (!scope_id || TAG(scope_id) != CLJ_VECTOR_PERSISTENT) return NULL;
    return as_persistent_vector(scope_id);
}

static inline void scope_stack_push(CljTransientVector *stack, CljPersistentVector *scope_vec) {
    if (!stack) return;
    // Scope stack is a transient vector wrapper.
    vector_push(stack, (ID)scope_vec);
}

static inline void scope_stack_pop(CljTransientVector *stack) {
    if (!stack) return;
    vector_pop(stack);
}

static bool lexical_lookup(CljTransientVector *scope_stack, CljSymbol *sym, uint8_t *out_depth, uint8_t *out_slot) {
    if (!scope_stack || !sym || !out_depth || !out_slot) return false;
    unsigned int cnt = scope_stack_count(scope_stack);
    for (unsigned int d = 0; d < cnt && d < 255; d++) {
        CljPersistentVector *scope = scope_stack_get_from_top(scope_stack, d);
        if (!scope) continue;
        unsigned int sc = vector_count(scope);
        ID *data = vector_as_array(scope);
        if (!data) continue;
        // Shadowing semantics: later bindings win, so scan from the end.
        for (unsigned int s = sc; s > 0; s--) {
            unsigned int idx = s - 1;
            if (idx >= 255) continue;
            if (data[idx] == (ID)sym) {
                *out_depth = (uint8_t)d;
                *out_slot = (uint8_t)idx;
                return true;
            }
            // Fallback: Some symbols are pre-interned (builtins) and may appear as distinct
            // objects with the same cname during canonicalization. Match locals by cname so
            // macro/function parameters like `name` work correctly.
            if (sym->cname && data[idx] && TAG(data[idx]) == CLJ_SYMBOL) {
                CljSymbol *bound = (CljSymbol*)data[idx];
                if (bound->cname && strcmp(bound->cname, sym->cname) == 0) {
                    *out_depth = (uint8_t)d;
                    *out_slot = (uint8_t)idx;
                    return true;
                }
            }
        }
    }
    return false;
}

// Cached destructure function (resolved once after bootstrap)
static ID destructure_fn = NULL;

// Call Clojure (destructure bindings), returns NULL if not available (bootstrap)
static CljPersistentVector* destructure(EvalState *st, CljPersistentVector *bindings) {
    if (!destructure_fn) {
        ID resolved = ns_resolve(st, SYM_DESTRUCTURE);
        if (resolved == NOT_FOUND || !resolved) return NULL;  // Bootstrap: not loaded yet
        destructure_fn = resolved;
    }
    
    ID args[] = { bindings };
    ID result = eval_function_call(destructure_fn, args, 1, NULL, st);
    return (TAG(result) == CLJ_VECTOR_PERSISTENT) ? as_persistent_vector(result) : NULL;
}

// Gensym counter for fn/defn/loop param destructuring
static unsigned long param_gensym_counter = 0;

// Transform params with destructuring, returns new_params and let_bindings
// Returns NULL for let_bindings if no destructuring needed
static CljPersistentVector* transform_params(EvalState *st, CljPersistentVector *params, CljPersistentVector **out_let_bindings) {
    unsigned int count = vector_count(params);
    CljPersistentVector *new_params = make_vector(count, false);
    CljPersistentVector *let_bindings = make_vector(count * 2, false);
    bool has_destructuring = false;
    
    VECTOR_FOR_EACH(params, param) {
        unsigned char tag = TAG(param);
        if (tag == CLJ_VECTOR_PERSISTENT || tag == CLJ_MAP_PERSISTENT) {
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
            // Avoid -Werror=format-truncation on toolchains that treat warnings as errors.
            // keyword_with_colon must fit ":" + sym_buf + '\0'
            size_t sym_buf_len = strlen(sym_buf);
            if (sym_buf_len + 2 > sizeof(keyword_with_colon)) {
                return NULL;
            }
            snprintf(keyword_with_colon, sizeof(keyword_with_colon), ":%.*s",
                     (int)(sizeof(keyword_with_colon) - 2), sym_buf);
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
            // Avoid -Werror=format-truncation (":" + keyword_name + '\0' must fit).
            size_t kw_len = strlen(keyword_name);
            if (kw_len + 2 > sizeof(keyword_with_colon)) {
                return NULL;
            }
            snprintf(keyword_with_colon, sizeof(keyword_with_colon), ":%.*s",
                     (int)(sizeof(keyword_with_colon) - 2), keyword_name);
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
static ID canonicalize_expr_with_scope(ID expr, EvalState *st, bool in_quote, CljTransientVector *scope_stack) {
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

    // Lexical addressing: rewrite *current-scope* symbol references to (depth=0, slot).
    // NOTE: We intentionally do NOT rewrite depth>0 here yet (closure-capture comes later).
    if (!in_quote && tag == CLJ_SYMBOL && scope_stack) {
        // Keywords evaluate to themselves and must never be rewritten.
        if (!IS_KEYWORD(expr)) {
            CljSymbol *sym = (CljSymbol*)expr;
            // Special forms are not shadowable; keep them as symbols so eval can dispatch.
            if (is_special_symbol(sym)) {
                return expr;
            }
            // Dynamic vars are late-bound and must not be lexicalized.
            if (!(sym->base.flags & CLJ_FLAG_DYNAMIC)) {
                uint8_t depth = 0, slot = 0;
                if (lexical_lookup(scope_stack, sym, &depth, &slot)) {
                    if (depth == 0) {
                        ID ref = (ID)make_slot_ref(sym, 0, slot);
                        if (ref) return AUTORELEASE(ref);
                    }
                }
            }
        }
    }
    
    // Handle lists and ASTNodes: Convert to ASTNode unless in_quote
    // NOTE: Parser produces ASTNodes, but we handle both for robustness
    if (is_list_type(tag)) {
        CljList *list = as_list(expr);
        CLJ_ASSERT(list != NULL);
        
        ID first = canonicalize_expr_with_scope(list->first, st, in_quote, scope_stack);
        
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
                // NOTE: Arguments are NOT fully canonicalized here - macros work with raw forms
                // BUT: Symbol tokens must be converted to interned symbols for frame lookup
                // The expanded form will be canonicalized recursively below
                ID args[16];
                int argc = 0;
                for (CljList *cur = list->rest ? as_list(list->rest) : NULL;
                     cur && argc < 16;
                     cur = cur->rest ? as_list(cur->rest) : NULL) {
                    ID arg = cur->first;
                    // Convert symbol tokens to interned symbols so frame_lookup works
                    // (frame_lookup uses pointer comparison for interned symbols)
                    if (arg && TAG(arg) == CLJ_SYMBOL_TOKEN) {
                        CljSymbolToken *token = (CljSymbolToken*)arg;
                        CljSymbol *sym = canonicalize_symbol_token(token, st);
                        if (sym) arg = sym;
                    }
                    args[argc++] = arg;
                }
                
                // Call macro function to expand the form.
                // IMPORTANT: Evaluate macro in the macro's defining namespace so that
                // unqualified helper symbols used by the macro body resolve consistently
                // (e.g. clojure.core macros calling core helpers while expanding in user ns).
                CljNamespace *saved_ns = st ? st->current_ns : NULL;
                if (st && macro && ((CljFunction*)macro)->ns) {
                    st->current_ns = ((CljFunction*)macro)->ns;
                }
                
                ID expanded = eval_function_call((CljObject*)macro, args, argc, NULL, st);
                if (st) {
                    st->current_ns = saved_ns;
                }
                if (!expanded) return NULL;
                
                // Transfer metadata from original form to expanded form
                move_meta(list, expanded);
                
                // CRITICAL: Check if expanded is an immediate value (e.g., (-> 5) returns 5)
                // Immediate values don't need wrapping and should be returned directly
                if (IS_IMMEDIATE(expanded)) {
                    // Return immediate value directly (canonicalize_expr handles this)
                    return canonicalize_expr_with_scope(expanded, st, in_quote, scope_stack);
                }
                
                // CRITICAL: If expanded is a CLJ_SEQ, it might be in the pool from macro expansion.
                // RETAIN it before recursively canonicalizing to ensure it survives.
                unsigned char expanded_tag = TAG(expanded);
                if (expanded_tag == CLJ_SEQ) {
                    RETAIN(expanded);
                }
                
                // CRITICAL: Ensure expanded form is a list-like type (CLJ_LIST or CLJ_AST_NODE)
                // Macros can return PersistentList (CLJ_LIST) which needs to be canonicalized
                if (!is_list_type(expanded_tag)) {
                    // Expanded form is not a list - this shouldn't happen for threading macros
                    // but handle it gracefully by wrapping in a list
                    expanded = AUTORELEASE(make_list(expanded, NULL));
                }
                
                // Recursively canonicalize the expanded form
                // This will convert CLJ_LIST to CLJ_AST_NODE and canonicalize all elements
                ID result = canonicalize_expr_with_scope(expanded, st, in_quote, scope_stack);
                
                // RELEASE the RETAIN'd expanded if it was a CLJ_SEQ
                if (expanded_tag == CLJ_SEQ) {
                    RELEASE(expanded);
                }
                
                return result;
            }
        }
        
        // ========== DESTRUCTURING TRANSFORMATION (compile-time) ==========
        // Transform let/loop bindings and fn/defn params
        if (!in_quote && first && TAG(first) == CLJ_SYMBOL) {
            CljSymbol *head_sym = as_symbol(first);

            // (let [bindings] body...) - expand bindings using Clojure destructure
            if (head_sym == SYM_LET) {
                CljList *rest1 = list->rest ? as_list(list->rest) : NULL;
                if (rest1 && rest1->first && TAG(rest1->first) == CLJ_VECTOR_PERSISTENT) {
                    CljPersistentVector *bindings = as_persistent_vector(rest1->first);
                    if (bindings_need_destructuring(bindings)) {
                        CljPersistentVector *expanded = destructure(st, bindings);
                        if (expanded) {
                            // Rebuild: (let expanded-bindings body...)
                            ID new_rest = AUTORELEASE(make_list(expanded, rest1->rest ? as_list(rest1->rest) : NULL));
                            ID new_list = AUTORELEASE(make_list(first, as_list(new_rest)));
                            return canonicalize_expr_with_scope(new_list, st, in_quote, scope_stack);
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
                if (rest1 && rest1->first && TAG(rest1->first) == CLJ_VECTOR_PERSISTENT) {
                    CljPersistentVector *bindings = as_persistent_vector(rest1->first);
                    if (bindings_need_destructuring(bindings)) {
                        static unsigned long gensym_counter = 0;
                        unsigned int count = vector_count(bindings);
                        CljPersistentVector *loop_bindings = make_vector(count, false);
                        CljPersistentVector *let_bindings = make_vector(count, false);
                        
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
                        CljPersistentVector *expanded_let_bindings = NULL;
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
                        return canonicalize_expr_with_scope(loop_form, st, in_quote, scope_stack);
                    }
                }
            }
            
            // (fn [params] body) or (fn name [params] body)
            if (head_sym == SYM_FN) {
                CljList *rest1 = list->rest ? as_list(list->rest) : NULL;
                if (rest1) {
                    ID second = rest1->first;
                    CljPersistentVector *params = NULL;
                    CljList *body_rest = NULL;
                    bool named = false;
                    
                    if (second && TAG(second) == CLJ_SYMBOL && !IS_KEYWORD(second)) {
                        // Named fn: (fn name [params] body)
                        named = true;
                        CljList *rest2 = rest1->rest ? as_list(rest1->rest) : NULL;
                        if (rest2 && rest2->first && TAG(rest2->first) == CLJ_VECTOR_PERSISTENT) {
                            params = as_persistent_vector(rest2->first);
                            body_rest = rest2->rest ? as_list(rest2->rest) : NULL;
                        }
                    } else if (second && TAG(second) == CLJ_VECTOR_PERSISTENT) {
                        // Anonymous fn: (fn [params] body)
                        params = as_persistent_vector(second);
                        body_rest = rest1->rest ? as_list(rest1->rest) : NULL;
                    }
                    
                    if (params && params_need_destructuring(params)) {
                        CljPersistentVector *expanded_let_bindings = NULL;
                        CljPersistentVector *new_params = transform_params(st, params, &expanded_let_bindings);
                        
                        if (expanded_let_bindings && vector_count(expanded_let_bindings) > 0) {
                            ID body = body_rest ? body_rest->first : NULL;
                            CljList *let_form = make_list(SYM_LET,
                                                          make_list(expanded_let_bindings,
                                                                    make_list(body, NULL)));
                            ID new_form = named
                                ? (ID)make_list(first, make_list(second, make_list(new_params, make_list(let_form, NULL))))
                                : (ID)make_list(first, make_list(new_params, make_list(let_form, NULL)));
                            return canonicalize_expr_with_scope(AUTORELEASE(new_form), st, in_quote, scope_stack);
                        }
                    }
                }
            }
            // Note: defn is a macro that expands to (def name (fn ...)),
            // so fn's destructuring handler above covers defn as well
        }
        // ========== END DESTRUCTURING TRANSFORMATION ==========

        // Lexical addressing for `let`: the body runs with a fresh CallFrame containing the let-bindings.
        // We canonicalize the bindings vector without lexical rewrite (init exprs should not see later bindings),
        // then push a scope of the let binding names for the body.
        if (!in_quote && first == SYM_LET && scope_stack) {
            CljList *rest1 = list->rest ? as_list(list->rest) : NULL;
            if (rest1 && rest1->first && TAG(rest1->first) == CLJ_VECTOR_PERSISTENT) {
                // Canonicalize bindings vector without lexical rewrite.
                ID canon_bindings = canonicalize_expr_with_scope(rest1->first, st, child_in_quote, NULL);

                // Build let-scope from binding names (even indices): [x expr y expr] -> scope [x y]
                CljPersistentVector *bindings_vec = as_persistent_vector(canon_bindings);
                unsigned int bc = bindings_vec ? vector_count(bindings_vec) : 0;
                unsigned int pair_count = (bc / 2);
                // let_scope wird innerhalb eines WITH_AUTORELEASE_POOL erstellt
                // Füge es zum Pool hinzu, damit es automatisch freigegeben wird
                CljPersistentVector *let_scope = AUTORELEASE(make_vector((int)pair_count, false));
                if (bindings_vec && let_scope) {
                    for (unsigned int i = 0; i + 1 < bc; i += 2) {
                        ID k = vector_nth(bindings_vec, (int)i);
                        // Destructuring should already have been expanded; keys should be symbols.
                        if (k && TAG(k) == CLJ_SYMBOL && !IS_KEYWORD(k) && !is_special_symbol((CljSymbol*)k)) {
                            vector_conj_inplace(&let_scope, k);
                        } else {
                            // If key isn't a plain symbol, keep a placeholder so slot indices stay aligned.
                            // (This should be rare; primarily defensive.)
                            vector_conj_inplace(&let_scope, NULL);
                        }
                    }
                }

                scope_stack_push(scope_stack, let_scope);
                // let_scope ist bereits im Pool, kein RELEASE nötig

                CljList *canon_body = rest1->rest
                    ? canonicalize_rest_to_plain_list((ID)rest1->rest, st, child_in_quote, scope_stack)
                    : NULL;

                scope_stack_pop(scope_stack);

                CljList *tail = (CljList*)AUTORELEASE(make_list(canon_bindings, canon_body));
                ID result = in_quote
                    ? AUTORELEASE(make_list(first, tail))
                    : AUTORELEASE(make_ast_node(first, (ID)tail));
                if (!result) return expr;
                move_meta(expr, result);
                return result;
            }
        }

        // Lexical addressing for `loop`: the body runs with a fresh CallFrame containing the loop-bindings.
        // IMPORTANT: Inside a loop, depth=0 refers to the loop frame. If we don't push a loop scope here,
        // outer function parameters (like `n`) can be incorrectly rewritten to (depth=0, slot=0) and
        // then read from the loop frame instead of the parent function frame.
        if (!in_quote && first == SYM_LOOP && scope_stack) {
            CljList *rest1 = list->rest ? as_list(list->rest) : NULL;
            if (rest1 && rest1->first && TAG(rest1->first) == CLJ_VECTOR_PERSISTENT) {
                // Canonicalize bindings vector without lexical rewrite.
                ID canon_bindings = canonicalize_expr_with_scope(rest1->first, st, child_in_quote, NULL);

                // Build loop-scope from binding names (even indices): [i init acc init] -> scope [i acc]
                CljPersistentVector *bindings_vec = as_persistent_vector(canon_bindings);
                unsigned int bc = bindings_vec ? vector_count(bindings_vec) : 0;
                unsigned int pair_count = (bc / 2);
                CljPersistentVector *loop_scope = make_vector((int)pair_count, false);
                if (bindings_vec && loop_scope) {
                    for (unsigned int i = 0; i + 1 < bc; i += 2) {
                        ID k = vector_nth(bindings_vec, (int)i);
                        // Destructuring should already have been expanded; keys should be symbols.
                        if (k && TAG(k) == CLJ_SYMBOL && !IS_KEYWORD(k) && !is_special_symbol((CljSymbol*)k)) {
                            vector_conj_inplace(&loop_scope, k);
                        } else {
                            // Keep placeholder to preserve slot indices.
                            vector_conj_inplace(&loop_scope, NULL);
                        }
                    }
                }

                scope_stack_push(scope_stack, loop_scope);
                RELEASE(loop_scope);

                // Canonicalize body forms with loop-scope active.
                CljList *canon_body = rest1->rest
                    ? canonicalize_rest_to_plain_list((ID)rest1->rest, st, child_in_quote, scope_stack)
                    : NULL;

                scope_stack_pop(scope_stack);

                CljList *tail = (CljList*)AUTORELEASE(make_list(canon_bindings, canon_body));
                ID result = in_quote
                    ? AUTORELEASE(make_list(first, tail))
                    : AUTORELEASE(make_ast_node(first, (ID)tail));
                if (!result) return expr;
                move_meta(expr, result);
                return result;
            }
        }

        // Lexical addressing for `dotimes`: the body runs with a fresh CallFrame binding the loop var.
        // We canonicalize the binding vector without lexical rewrite (name + count expr),
        // then push a scope with just the loop var for the body.
        if (!in_quote && first == SYM_DOTIMES && scope_stack) {
            CljList *rest1 = list->rest ? as_list(list->rest) : NULL;
            if (rest1 && rest1->first && TAG(rest1->first) == CLJ_VECTOR_PERSISTENT) {
                ID canon_binding_vec = canonicalize_expr_with_scope(rest1->first, st, child_in_quote, NULL);
                CljPersistentVector *binding_vec = as_persistent_vector(canon_binding_vec);

                // Extract loop var (first element in [i n]).
                ID loop_var = (binding_vec && vector_count(binding_vec) >= 1)
                    ? vector_nth(binding_vec, 0)
                    : NULL;

                // Push scope [i] for the dotimes body so `i` can become a SlotRef.
                // dotimes_scope wird innerhalb eines WITH_AUTORELEASE_POOL erstellt
                // Füge es zum Pool hinzu, damit es automatisch freigegeben wird
                CljPersistentVector *dotimes_scope = AUTORELEASE(make_vector(1, false));
                if (loop_var && TAG(loop_var) == CLJ_SYMBOL && !IS_KEYWORD(loop_var)) {
                    vector_conj_inplace(&dotimes_scope, loop_var);
                } else {
                    vector_conj_inplace(&dotimes_scope, NULL);
                }
                scope_stack_push(scope_stack, dotimes_scope);
                // dotimes_scope ist bereits im Pool, kein RELEASE nötig

                CljList *canon_body = rest1->rest
                    ? canonicalize_rest_to_plain_list((ID)rest1->rest, st, child_in_quote, scope_stack)
                    : NULL;

                scope_stack_pop(scope_stack);

                CljList *tail = (CljList*)AUTORELEASE(make_list(canon_binding_vec, canon_body));
                ID result = in_quote
                    ? AUTORELEASE(make_list(first, tail))
                    : AUTORELEASE(make_ast_node(first, (ID)tail));
                if (!result) return expr;
                move_meta(expr, result);
                return result;
            }
        }

        // Lexical addressing (step 1): rewrite references to fn-parameters inside the fn body.
        // IMPORTANT: Do NOT lexicalize the fn name or parameter declarations themselves.
        if (!in_quote && first == SYM_FN) {
            CljList *rest1 = list->rest ? as_list(list->rest) : NULL;
            if (rest1) {
                ID second = rest1->first;          // name OR params
                ID params_form = NULL;            // params vector
                CljList *body_rest = NULL;        // list of body forms
                bool named = false;

                if (second && TAG(second) == CLJ_SYMBOL && !IS_KEYWORD(second)) {
                    named = true;
                    CljList *rest2 = rest1->rest ? as_list(rest1->rest) : NULL;
                    params_form = rest2 ? rest2->first : NULL;
                    body_rest = (rest2 && rest2->rest) ? as_list(rest2->rest) : NULL;
                } else {
                    params_form = second;
                    body_rest = rest1->rest ? as_list(rest1->rest) : NULL;
                }

                if (params_form && TAG(params_form) == CLJ_VECTOR_PERSISTENT) {
                    // Canonicalize fn-name/params with lexical rewrite disabled.
                    ID name_canon = named ? canonicalize_expr_with_scope(second, st, child_in_quote, NULL) : NULL;
                    ID params_canon_id = canonicalize_expr_with_scope(params_form, st, child_in_quote, NULL);
                    CljPersistentVector *params_vec = as_persistent_vector(params_canon_id);

                    // Only handle non-variadic, simple-symbol parameter vectors for now.
                    bool ok = params_vec != NULL;
                    bool variadic = false;
                    if (ok) {
                        VECTOR_FOR_EACH(params_vec, p) {
                            if (p == SYM_AMP) { variadic = true; break; }
                            if (!p || TAG(p) != CLJ_SYMBOL || IS_KEYWORD(p)) { ok = false; break; }
                        }
                    }

                    if (ok && !variadic) {
                        // Build a scope-vector where index == CallFrame slot index.
                        unsigned int pc = vector_count(params_vec);
                        // param_scope wird innerhalb eines WITH_AUTORELEASE_POOL erstellt
                        // Füge es zum Pool hinzu, damit es automatisch freigegeben wird
                        CljPersistentVector *param_scope = AUTORELEASE(make_vector((int)pc, false));
                        VECTOR_FOR_EACH(params_vec, p) {
                            vector_conj_inplace(&param_scope, p);
                        }

                        // IMPORTANT: Each fn gets its own scope stack for now (depth=0 only).
                        // This prevents accidentally rewriting free variables (depth>0) before
                        // closure-capture support exists.
                        // Initialgröße 8: verschachtelte Scopes innerhalb einer fn
                        CljPersistentVector *persistent_fn_vec = make_vector(8, false);
                        CljTransientVector *fn_scope_stack = as_transient_vector(vector_transient(persistent_fn_vec));
                        RELEASE(persistent_fn_vec);
                        
                        scope_stack_push(fn_scope_stack, param_scope);
                        // param_scope ist bereits im Pool, kein RELEASE nötig

                        // Canonicalize body with param scope active.
                        CljList *canon_body = body_rest
                            ? canonicalize_rest_to_plain_list((ID)body_rest, st, child_in_quote, fn_scope_stack)
                            : NULL;

                        scope_stack_pop(fn_scope_stack);
                        RELEASE(fn_scope_stack);

                        // Rebuild rest list: (name? params body...)
                        CljList *tail = NULL;
                        if (named) {
                            CljList *params_and_body = (CljList*)AUTORELEASE(make_list(params_canon_id, canon_body));
                            tail = (CljList*)AUTORELEASE(make_list(name_canon, params_and_body));
                        } else {
                            tail = (CljList*)AUTORELEASE(make_list(params_canon_id, canon_body));
                        }

                        ID result = in_quote
                            ? AUTORELEASE(make_list(first, tail))
                            : AUTORELEASE(make_ast_node(first, (ID)tail));
                        if (!result) return expr;
                        move_meta(expr, result);
                        return result;
                    }
                }
            }
        }
        
        // Canonicalize rest of list into plain CLJ_LIST cons cells.
        // This ensures only the callsite head becomes CLJ_AST_NODE.
        CljList *rest_list = list->rest ? canonicalize_rest_to_plain_list(list->rest, st, child_in_quote, scope_stack) : NULL;

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
    if (tag == CLJ_VECTOR_PERSISTENT) {
        CljPersistentVector *vec = (CljPersistentVector*)expr;
        CLJ_ASSERT(vec != NULL);
        int count = vector_count(vec);
        
        // Stack buffer for small vectors (avoid malloc)
        ID stack_buf[16];
        ID *canon_elems = (count <= 16) ? stack_buf : (ID*)malloc(count * sizeof(ID));
        CLJ_ASSERT(canon_elems != NULL && "Out of memory");
        
        bool changed = false;
        int i = 0;
        VECTOR_FOR_EACH(vec, elem) {
            canon_elems[i] = canonicalize_expr_with_scope(elem, st, in_quote, scope_stack);
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
        CljPersistentVector *new_vec = make_vector(count, false);
        for (int i = 0; i < count; i++) {
            ASSIGN(new_vec, vector_conj(new_vec, canon_elems[i]));
        }
        move_meta(vec, new_vec);
        
        if (count > 16) free(canon_elems);
        return AUTORELEASE(new_vec);
    }
    
    if (tag == CLJ_MAP_PERSISTENT) {
        CljMap *map = (CljMap*)expr;
        CLJ_ASSERT(map != NULL);
        int cnt = map_count(map);
        if (cnt <= 0) {
            return expr;
        }

        // Canonicalize keys and values (single pass), but only rebuild the map if something changed.
        // NOTE: We must not drop entries that appear before the first changed entry.
        ID stack_pairs[32 * 2];
        ID *pairs = (cnt <= 32) ? stack_pairs : (ID*)malloc((size_t)cnt * 2 * sizeof(ID));
        CLJ_ASSERT(pairs != NULL && "Out of memory");

        bool changed = false;
        int i = 0;
        MAP_FOR_EACH(map, key, value) {
            ID canon_key = canonicalize_expr_with_scope(key, st, in_quote, scope_stack);
            ID canon_value = canonicalize_expr_with_scope(value, st, in_quote, scope_stack);
            if (canon_key != key || canon_value != value) {
                changed = true;
            }
            pairs[i * 2] = canon_key;
            pairs[i * 2 + 1] = canon_value;
            i++;
        }

        if (!changed) {
            if (pairs != stack_pairs) free(pairs);
            return expr;  // No changes needed
        }

        CljMap *new_map = make_map(cnt);
        for (int j = 0; j < cnt; j++) {
            ID k = pairs[j * 2];
            ID v = pairs[j * 2 + 1];
            ASSIGN(new_map, map_assoc(new_map, k, v));
        }
        move_meta(map, new_map);

        if (pairs != stack_pairs) free(pairs);
        return AUTORELEASE(new_map);
    }
    
    // Other types don't need canonicalization
    return expr;
}

static ID canonicalize_expr(ID expr, EvalState *st, bool in_quote) {
    // Erstelle transient vector für scope_stack außerhalb des WITH_AUTORELEASE_POOL
    // Transient vectors sind für in-place Modifikationen optimiert
    // Initialgröße 8: typische verschachtelte Scopes (let, loop, dotimes, fn) passen hinein
    CljPersistentVector *persistent_vec = make_vector(8, false);
    CljTransientVector *scope_stack = as_transient_vector(vector_transient(persistent_vec));
    RELEASE(persistent_vec);
    
    ID out = canonicalize_expr_with_scope(expr, st, in_quote, scope_stack);
    
    // scope_stack ist ein transient vector, der außerhalb des Pools erstellt wurde
    // Freigeben nach Verwendung
    RELEASE(scope_stack);
    
    return out;
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

    // Canonicalization can allocate a lot of temporary AUTORELEASE containers.
    // Scope that churn to a nested pool so it doesn't inflate the caller's pool.
    // If the pool implementation ever becomes draining again, retain the result
    // inside the pool so it survives pop, then re-autorelease it into the
    // caller's pool.
    WITH_AUTORELEASE_POOL({
        result = canonicalize_expr(parsed_expr, st, false);

        RETAIN(result);

    });
    // result->rc should be at least 1

    return AUTORELEASE(result);
}


