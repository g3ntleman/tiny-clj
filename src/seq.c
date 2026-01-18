/*
 * Seq Implementation for Tiny-CLJ
 * 
 * Stack-allocated iterator with zero-copy semantics.
 * Optimized for embedded systems.
 */

#include "seq.h"
#include "builtins.h"  // builtin_get_eval_state, builtin_set_eval_state, native_first/rest/seq
#include "eval.h"      // eval_function_call
#include "value.h"
#include "list.h"
#include "vector.h"
#include "strings.h"
#include "map.h"
#include "symbol.h"
#include "memory.h"    // For subjective_c_register_release_fn
#include <string.h>
#include <stdlib.h>

// Unit tests may define this symbol to provide an EvalState for LazySeq
// realization. Provide a weak NULL default so non-test binaries link.
__attribute__((weak)) EvalState* g_test_eval_state = NULL;

CljLazySeq* make_lazy_seq(ID thunk) {
    if (!thunk) return NULL;

    CljLazySeq *lazy = (CljLazySeq*)malloc(sizeof(CljLazySeq));
    if (!lazy) return NULL;

    lazy->base.type = CLJ_LAZY_SEQ;
    lazy->base.rc = 1;
    lazy->base.flags = 0;

    // NOT_FOUND indicates "not realized".
    lazy->first = NOT_FOUND;
    lazy->thunk = RETAIN(thunk);
    lazy->cached_rest = NOT_FOUND;

    return lazy;
}

static void lazy_seq_realize(CljLazySeq *lazy) {
    if (!lazy) return;
    if (lazy->first != NOT_FOUND && lazy->cached_rest != NOT_FOUND) {
        return;
    }

    // No generator: treat as empty.
    if (!lazy->thunk) {
        lazy->first = NULL;
        lazy->cached_rest = NULL;
        return;
    }

    // Resolve EvalState.
    EvalState *st = builtin_get_eval_state();
    if (!st) {
        if (g_test_eval_state) {
            st = g_test_eval_state;
        } else {
            st = get_global_eval_state();
        }
    }

    if (!st) {
        // Cannot evaluate without state.
        lazy->first = NULL;
        lazy->cached_rest = NULL;
        return;
    }

    // Evaluate thunk once to produce the sequence body.
    builtin_set_eval_state(st);
    ID seq_val = eval_function_call(lazy->thunk, NULL, 0, NULL, st);

    ID first_val = NULL;
    ID rest_val = NULL;

    if (seq_val) {
        // Normalize through seq/first/rest to preserve existing semantics
        // (notably: nil elements vs empty sequences).
        ID seq_args[1] = {seq_val};
        ID seq_obj = native_seq(seq_args, 1);
        if (seq_obj) {
            ID one_arg[1] = {seq_obj};
            first_val = native_first(one_arg, 1);
            rest_val = native_rest(one_arg, 1);

            // Important: if the sequence is non-empty but its first element is
            // nil, native_first returns NULL. Store SYM_NIL internally so we
            // can distinguish (nil) from an empty sequence.
            if (!first_val) {
                first_val = SYM_NIL;
            }
        }
    }
    builtin_set_eval_state(NULL);

    // Cache results and release generator.
    // Use ASSIGN to retain cached values and release previous sentinels safely.
    ASSIGN(lazy->first, first_val);
    ASSIGN(lazy->cached_rest, rest_val);
    RELEASE(lazy->thunk);
    lazy->thunk = NULL;
}

static ID make_map_entry_vector(CljMap *map, int index) {
    if (!map || index < 0 || index >= map->count) {
        return NULL;
    }

    CljObject *key = map->data[index * 2];
    CljObject *value = map->data[index * 2 + 1];

    CljVector *entry = make_vector(2, CLJ_VECTOR);
    if (!entry) {
        return NULL;
    }

    entry = vector_conj(entry, key);
    entry = vector_conj(entry, value);

    return AUTORELEASE(entry);
}

// ============================================================================
// FAST SEQ IMPLEMENTATION
// ============================================================================

bool seq_iter_init(SeqIterator *iter, ID obj) {
    if (!iter) return false;
    
    // Initialize to empty
    memset(iter, 0, sizeof(SeqIterator));
    
    // Handle nil (now represented as NULL)
    if (!obj) {
        // Empty sequence - don't set seq_type, leave it as 0
        return true;  // Empty sequence, but valid
    }
    
    iter->container = obj;
    CljObject *o = obj;
    
    switch (o->type) {
        case CLJ_LIST:
        case CLJ_AST_NODE: {
            CljList *list_data = as_list(obj);
            // Note: LIST_FIRST can be NULL (nil) - it's a valid value in Clojure lists
            // A list is only empty if list_data itself is NULL or the list structure is invalid
            // We check if list_data is valid and has a structure (even if first element is nil)
            if (!list_data) {
                // Empty list - don't set seq_type, leave it as 0
                return true;  // Empty list
            }
            
            // Note: Empty list singleton is handled above (list_data check)
            // In Clojure, () is nil, not an empty list
            // empty_list() singleton is only used by (list) function
            
            // Store the list node itself, not the first element
            // Note: LIST_FIRST(list_data) can be NULL (nil) - this is valid
            iter->state.list.current = (CljObject*)list_data;
            iter->state.list.index = 0;
            iter->seq_type = CLJ_LIST;
            return true;
        }
        
        case CLJ_SEQ: {
            // Already a sequence - copy the embedded iterator state
            CljSeqIterator *seq = as_seq(obj);
            if (!seq) {
                // Empty seq - don't set seq_type, leave it as 0
                return true;  // Empty seq
            }
            
            // Copy the embedded iterator state
            *iter = seq->iter;  // Struct copy
            return true;
        }

        case CLJ_LAZY_SEQ: {
            // Lazy sequence - iterate by repeatedly taking rest.
            iter->state.list.current = (CljObject*)obj;
            iter->state.list.index = 0;
            iter->seq_type = CLJ_LAZY_SEQ;
            return true;
        }
        
        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT_WEAK:
        case CLJ_VECTOR_TRANSIENT: {
            CljVector *vec = as_vector(obj);
            
            // Initialize vector iterator using public API
            unsigned int count = vector_count(vec);
            if (count == 0) {
                return true;  // Empty vector
            }
            
            iter->state.vec.index = 0;
            iter->state.vec.count = count;
            iter->state.vec.data = NULL;  // Don't expose internal pointer
            iter->seq_type = CLJ_VECTOR;
            return true;
        }
        
        case CLJ_STRING: {
            CljString *str = (CljString*)obj;
            
            // Special case: empty string singleton
            if (str == string_empty_singleton) {
                // Empty string - don't set seq_type, leave it as 0
                return true;  // Empty string
            }
            
            // Access string data directly
            iter->state.str.data = str->data;
            iter->state.str.index = 0;
            iter->state.str.length = str->length;
            iter->seq_type = CLJ_STRING;
            return true;
        }
        
        case CLJ_MAP: {
            CljMap *map = as_map(obj);
            if (map->count == 0) {
                return true;  // Empty map
            }

            iter->state.map.map = (struct CljMap *)map;
            iter->state.map.index = 0;
            iter->state.map.count = map->count;
            iter->seq_type = CLJ_MAP;
            return true;
        }

        // Note: nil is now represented as NULL, handled above
        return true;
        
        default:
            return false;  // Not seqable
    }
}

ID seq_iter_first(const SeqIterator *iter) {
    if (!iter || seq_iter_empty(iter)) {
        return NULL;
    }
    
    switch (iter->seq_type) {
        case CLJ_LIST: {
            if (iter->state.list.current) {
                CljList *node = as_list(iter->state.list.current);
                ID elem = LIST_FIRST(node);
                // Convert SYM_NIL to NULL (nil representation)
                return (elem == SYM_NIL) ? NULL : elem;
            }
            return NULL;
        }

        case CLJ_LAZY_SEQ: {
            CljLazySeq *lazy = as_lazy_seq(iter->state.list.current);
            if (!lazy) return NULL;
            lazy_seq_realize(lazy);
            ID first = lazy->first;
            return (first == SYM_NIL) ? NULL : first;
        }
        
        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT_WEAK:
        case CLJ_VECTOR_TRANSIENT: {
            if (iter->state.vec.index < iter->state.vec.count) {
                // vector_nth returns element with lifetime tied to vector - no retain needed
                CljVector *vec = (CljVector*)iter->container;
                ID elem = vector_nth(vec, iter->state.vec.index);
                // Convert SYM_NIL to NULL (nil representation)
                return (elem == SYM_NIL) ? NULL : elem;
            }
            return NULL;
        }
        
        case CLJ_STRING: {
            if (iter->state.str.index < iter->state.str.length) {
                // Return character as integer
                char c = iter->state.str.data[iter->state.str.index];
                return fixnum((int)c);
            }
            return NULL;
        }

        case CLJ_MAP: {
            if (iter->state.map.index < iter->state.map.count) {
                return make_map_entry_vector((CljMap *)iter->state.map.map, iter->state.map.index);
            }
            return NULL;
        }
        
        default:
            return NULL;
    }
}

bool seq_iter_next(SeqIterator *iter) {
    if (!iter || seq_iter_empty(iter)) {
        return false;
    }
    
    switch (iter->seq_type) {
        case CLJ_LIST: {
            if (iter->state.list.current) {
                CljList *node = as_list(iter->state.list.current);
                CljObject *rest = LIST_REST(node);

                // If rest is a proper list node, keep iterating list nodes.
                // Use list_empty to properly handle list with nil element.
                if (rest && list_type_matches(TAG(rest))) {
                    CljList *rest_list = as_list(rest);
                    if (!list_empty(rest_list)) {
                        iter->state.list.current = rest;
                        iter->state.list.index++;
                        return true;
                    }
                }

                // Support "improper" list tails that are still seqable (e.g. LazySeq).
                // Advance by re-initializing the iterator from the tail.
                if (rest && is_seqable(rest)) {
                    if (seq_iter_init(iter, rest)) {
                        return !seq_iter_empty(iter);
                    }
                }
            }
            // Mark as exhausted
            iter->state.list.current = NULL;
            return false;
        }

        case CLJ_LAZY_SEQ: {
            CljLazySeq *lazy = as_lazy_seq(iter->state.list.current);
            if (!lazy) return false;
            lazy_seq_realize(lazy);
            ID rest = lazy->cached_rest;

            // Advance by re-initializing iterator from cached rest.
            // This preserves laziness (rest may itself be a LazySeq).
            if (!seq_iter_init(iter, rest)) {
                return false;
            }
            return !seq_iter_empty(iter);
        }
        
        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT_WEAK:
        case CLJ_VECTOR_TRANSIENT: {
            if (iter->state.vec.index < iter->state.vec.count - 1) {
                iter->state.vec.index++;
                return true;
            }
            // Mark as exhausted
            iter->state.vec.index = iter->state.vec.count;
            return false;
        }
        
        case CLJ_STRING: {
            if (iter->state.str.index < iter->state.str.length - 1) {
                iter->state.str.index++;
                return true;
            }
            // Mark as exhausted
            iter->state.str.index = iter->state.str.length;
            return false;
        }

        case CLJ_MAP: {
            if (iter->state.map.index < iter->state.map.count - 1) {
                iter->state.map.index++;
                return true;
            }
            iter->state.map.index = iter->state.map.count;
            return false;
        }
        
        default:
            return false;
    }
}

bool seq_iter_empty(const SeqIterator *iter) {
    if (!iter) return true;
    
    // Check if container is nil
    if (!iter->container) return true;
    
    // Check if container is an empty collection singleton
    if (is_singleton(iter->container)) {
        // Check if it's actually empty based on type
        switch (iter->container->type) {
            case CLJ_VECTOR:
            case CLJ_VECTOR_TRANSIENT_WEAK:
            case CLJ_VECTOR_TRANSIENT: {
                CljVector *vec = (CljVector*)iter->container;
                return vector_count(vec) == 0;
            }
            case CLJ_LIST: {
                CljList *list = (CljList*)iter->container;
                // Use list_empty to properly handle list with nil element
                return list_empty(list);
            }
            case CLJ_STRING: {
                CljString *str = (CljString*)iter->container;
                return str == string_empty_singleton || str->length == 0;
            }
            default:
                return true;
        }
    }
    
    // Check based on seq_type for non-empty sequences
    switch (iter->seq_type) {
        case CLJ_LIST:
            return iter->state.list.current == NULL;

        case CLJ_LAZY_SEQ: {
            CljLazySeq *lazy = as_lazy_seq(iter->state.list.current);
            if (!lazy) return true;
            lazy_seq_realize(lazy);
            return lazy->first == NULL && lazy->cached_rest == NULL;
        }
        
        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT_WEAK:
        case CLJ_VECTOR_TRANSIENT:
            return iter->state.vec.index >= iter->state.vec.count;
        
        case CLJ_STRING:
            return iter->state.str.index >= iter->state.str.length;

        case CLJ_MAP:
            return iter->state.map.index >= iter->state.map.count;
        
        default:
            // If seq_type is 0 (not set), it's an empty sequence
            return iter->seq_type == 0;
    }
}

int seq_iter_position(const SeqIterator *iter) {
    if (!iter) return 0;
    
    switch (iter->seq_type) {
        case CLJ_LIST:
            return iter->state.list.index;
        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT_WEAK:
        case CLJ_VECTOR_TRANSIENT:
            return iter->state.vec.index;
        case CLJ_STRING:
            return iter->state.str.index;
        case CLJ_MAP:
            return iter->state.map.index;
        default:
            return 0;
    }
}

// ============================================================================
// COMPATIBILITY LAYER (Heap-based API)
// ============================================================================

CljSeqIterator* make_seq(ID obj) {
    // Handle nil and empty collections - return nil singleton
    if (!obj) return NULL;
    
    unsigned char obj_tag = TAG(obj);
    
    // If already a CLJ_SEQ, return it directly (no need to wrap again)
    if (obj_tag == CLJ_SEQ) {
        CljSeqIterator *seq = as_seq(obj);
        return seq;  // Already a seq, return as-is
    }
    
    // Check if collection is empty
    if (obj_tag == CLJ_VECTOR) {
        CljVector *vec = as_vector((CljObject*)obj);
        if (vec && vector_count(vec) == 0) return NULL;
    } else if (list_type_matches(obj_tag)) {
        CljList *list = as_list((CljObject*)obj);
        if (list_empty(list)) return NULL;
    } else if (obj_tag == CLJ_MAP || obj_tag == CLJ_MAP_TRANSIENT) {
        CljMap *map = as_map(obj);
        if (!map || map->count == 0) return NULL;
    }
    
    // Allocate heap wrapper
    // Use malloc instead of calloc - all fields are immediately initialized
    CljSeqIterator *heap_seq = (CljSeqIterator*)malloc(sizeof(CljSeqIterator));
    if (!heap_seq) return NULL;
    
    heap_seq->base.type = CLJ_SEQ;
    heap_seq->base.rc = 1;
    
    // Initialize embedded stack iterator
    if (!seq_iter_init(&heap_seq->iter, (CljObject*)obj)) {
        free(heap_seq);
        return NULL;  // Empty or not seqable
    }
    
    // If iterator is empty, return nil (NULL) - JVM-compatible
    if (seq_iter_empty(&heap_seq->iter)) {
        free(heap_seq);
        return NULL;
    }
    
    return heap_seq;
}

void seq_release(ID seq_obj) {
    if (!seq_obj) return;
    CljSeqIterator *seq = as_seq(seq_obj);
    if (!seq) return;
    
    // Stack iterator doesn't need cleanup
    free(seq);
}

ID seq_first(ID seq_obj) {
    if (!seq_obj) return NULL;
    CljSeqIterator *seq = as_seq(seq_obj);
    if (!seq) return NULL;
    
    return seq_iter_first(&seq->iter);
}

ID seq_rest(ID seq_obj) {
    if (!seq_obj) return NULL;
    CljSeqIterator *seq = as_seq(seq_obj);
    if (!seq) return NULL;
    
    // Create new heap wrapper with advanced iterator
    // Use malloc instead of calloc - all fields are immediately initialized
    CljSeqIterator *rest_seq = (CljSeqIterator*)malloc(sizeof(CljSeqIterator));
    if (!rest_seq) return NULL;
    
    rest_seq->base.type = CLJ_SEQ;
    rest_seq->base.rc = 1;
    
    // Copy iterator state
    rest_seq->iter = seq->iter;  // Struct copy
    seq_iter_next(&rest_seq->iter);
    
    return (CljObject*)rest_seq;
}

ID seq_next(ID seq_obj) {
    if (!seq_obj) return NULL;
    
    // CRITICAL: If the original sequence was a CLJ_LIST, return CLJ_LIST directly
    // This matches the behavior of native_next in builtins.c
    CljSeqIterator *seq = as_seq(seq_obj);
    if (seq && seq->iter.seq_type == CLJ_LIST) {
        // Original was a CLJ_LIST - return CLJ_LIST directly (not CLJ_SEQ)
        if (seq->iter.state.list.current) {
            CljList *current_list = as_list(seq->iter.state.list.current);
            if (current_list) {
                CljObject *rest = LIST_REST(current_list);
                // next returns nil if rest is empty, otherwise rest
                // rest is part of the original list structure, which is already safe (caller has strong reference)
                return rest;
            }
        }
        // Empty list - return nil
        return NULL;
    }

    // For other types (CLJ_VECTOR, CLJ_STRING, etc.), use seq_rest
    // Get rest sequence (DRY: reuse seq_rest implementation)
    ID rest_seq = seq_rest(seq_obj);
    if (!rest_seq) return NULL;
    
    // Check if rest is empty - if so, return nil (Clojure-compatible)
    if (seq_empty(rest_seq)) {
        seq_release(rest_seq);
        return NULL;  // nil
    }
    
    // Rest is non-empty, return it
    return rest_seq;
}

ID seq_next_inplace(ID seq_obj) {
    if (!seq_obj) return NULL;
    
    CljSeqIterator *seq = as_seq(seq_obj);
    if (!seq) return NULL;
    
    if (seq->iter.seq_type == CLJ_LIST) {
        return seq_next(seq_obj);
    }
    
    if (!seq_iter_next(&seq->iter)) {
        return NULL;
    }
    
    return seq_obj;
}

bool seq_empty(ID seq_obj) {
    if (!seq_obj) return true;
    CljSeqIterator *seq = as_seq(seq_obj);
    if (!seq) return true;
    
    return seq_iter_empty(&seq->iter);
}

int seq_count(ID obj) {
    if (!obj) return 0;
    
    // If it's already a seq wrapper, count from iterator state
    if (TAG(obj) == CLJ_SEQ) {
        CljSeqIterator *seq = as_seq(obj);
        if (!seq) return 0;
        
        // Get count from embedded iterator state (remaining elements)
        switch (seq->iter.seq_type) {
            case CLJ_VECTOR:
            case CLJ_VECTOR_TRANSIENT_WEAK:
            case CLJ_VECTOR_TRANSIENT:
                // Return remaining elements, not total count
                return seq->iter.state.vec.count - seq->iter.state.vec.index;
            case CLJ_LIST:
                // List doesn't have direct count in state, fall through to iterate
                break;
            case CLJ_STRING:
                // Return remaining characters, not total length
                return seq->iter.state.str.length - seq->iter.state.str.index;
            case CLJ_MAP:
                // Return remaining entries, not total count
                return seq->iter.state.map.count - seq->iter.state.map.index;
            default:
                return 0;
        }
    }
    
    // Fast path for vectors - O(1)
    if (TAG(obj) == CLJ_VECTOR) {
        CljVector *vec = as_vector((CljObject*)obj);
        return vec ? vector_count(vec) : 0;
    }
    
    // Fallback: iterate and count - O(n)
    SeqIterator iter;
    if (!seq_iter_init(&iter, (CljObject*)obj)) return 0;
    
    int count = 0;
    while (!seq_iter_empty(&iter)) {
        count++;
        seq_iter_next(&iter);
    }
    return count;
}

// ============================================================================
// SEQABLE PREDICATES (Compatibility)
// ============================================================================

bool is_seqable(ID obj) {
    if (!obj) return true; // nil is seqable
    
    switch (((CljObject*)obj)->type) {
        case CLJ_LIST:
        case CLJ_AST_NODE:
        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT_WEAK:
        case CLJ_VECTOR_TRANSIENT:
        case CLJ_MAP:
        case CLJ_STRING:
        case CLJ_SEQ:  // Sequences are seqable
        case CLJ_LAZY_SEQ:
        // Note: nil is now represented as NULL
            return true;
        default:
            return false;
    }
}

bool is_seq(ID obj) {
    if (!obj) return false;
    if (list_type_matches(TAG(obj))) {
        return true;
    }
    return TAG(obj) == CLJ_SEQ || TAG(obj) == CLJ_LAZY_SEQ;
}

// ============================================================================
// RELEASE HANDLER REGISTRATION
// ============================================================================

/**
 * @brief Release handler for CljLazySeq objects.
 * 
 * Called by subjective-c memory.c when a CljLazySeq's reference count reaches zero.
 * Releases the thunk, first element, and cached rest sequence.
 */
static void release_lazy_seq(CljObject *v) {
    CljLazySeq *lazy_seq = (CljLazySeq*)v;
    if (lazy_seq) {
        RELEASE(lazy_seq->thunk);
        RELEASE(lazy_seq->first);
        RELEASE(lazy_seq->cached_rest);
    }
}

/**
 * @brief Register seq-related release handlers with subjective-c memory system.
 * 
 * Should be called during runtime initialization.
 */
void seq_register_release_fn(void) {
    subjective_c_register_release_fn(CLJ_LAZY_SEQ, release_lazy_seq);
}

