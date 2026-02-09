/*
 * Seq Implementation for Tiny-CLJ
 * 
 * Stack-allocated iterator with zero-copy semantics.
 * Optimized for embedded systems.
 */

#include "seq.h"
#include "builtins.h"  // builtin_get_eval_state, builtin_set_eval_state, native_first/rest/seq
#include "eval.h"      // eval_function_call
#include "function.h"  // CljFunction (needed for thunk->ns)
#include "value.h"
#include "list.h"
#include "vector.h"
#include "strings.h"
#include "map.h"
#include "hashset.h"
#include "symbol.h"
#include "memory.h"    // For subjective_c_register_release_fn
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Unit tests may define this symbol to provide an EvalState for LazySeq
// realization. Provide a weak NULL default so non-test binaries link.
__attribute__((weak)) EvalState* g_test_eval_state = NULL;

CljLazySeq* make_lazy_seq(ID thunk) {
    if (!thunk) return NULL;

    CljLazySeq *lazy = ALLOC(CljLazySeq, 1);
    if (!lazy) return NULL;

    lazy->base.type = CLJ_LAZY_SEQ;
    lazy->base.flags = 0;
    lazy->first = NOT_FOUND;
    lazy->thunk = RETAIN(thunk);
    lazy->cached_rest = NOT_FOUND;
    lazy->thunk_state = NULL;
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
    //
    // CRITICAL (Clojure compatibility):
    // Realize the thunk in the thunk's defining namespace, not the caller's
    // current namespace. Otherwise unqualified symbol resolution inside the
    // lazy body can fail at realization time (e.g. private helpers in the same ns).
    builtin_set_eval_state(st);
    CljNamespace *saved_ns = st->current_ns;
    CLJ_ASSERT(lazy->thunk && !IS_IMMEDIATE(lazy->thunk) && 
               (TAG(lazy->thunk) == CLJ_CLOSURE || TAG(lazy->thunk) == CLJ_FUNC));
    ID seq_val;
    ID first_val = NULL;
    ID rest_val = NULL;
    if (lazy->thunk_state && is_persistent_map(lazy->thunk_state)) {
        ID state = lazy->thunk_state;
        if (TAG(lazy->thunk) == CLJ_CLOSURE) {
            CljFunction *thunk_fn = (CljFunction*)lazy->thunk;
            if (thunk_fn->ns) st->current_ns = thunk_fn->ns;
        }
        seq_val = eval_function_call(lazy->thunk, &state, 1, NULL, st);
        st->current_ns = saved_ns;
        goto realized;
    }
    if (TAG(lazy->thunk) == CLJ_CLOSURE) {
        CljFunction *thunk_fn = (CljFunction*)lazy->thunk;
        if (thunk_fn->ns) st->current_ns = thunk_fn->ns;
    }
    seq_val = eval_function_call(lazy->thunk, NULL, 0, NULL, st);  // 0-arity (e.g. lazy-seq*)
    st->current_ns = saved_ns;
realized:
    ;

    if (seq_val) {
        // Normalize through seq/first/rest to preserve existing semantics
        // (notably: nil elements vs empty sequences).
        ID seq_args[1] = {seq_val};
        ID seq_obj = native_seq(seq_args, 1);
        if (seq_obj) {
            ID one_arg[1] = {seq_obj};
            first_val = native_first(one_arg, 1);
            rest_val = native_rest(one_arg, 1);

            // Empty sequence: first and rest both nil -> leave first_val/rest_val NULL.
            // Sequence (nil . rest): first is nil but rest non-nil -> store SYM_NIL so we have one element.
            if (!first_val && rest_val) {
                first_val = SYM_NIL;
            }
        }
    }
    builtin_set_eval_state(NULL);

    // Cache results and release generator.
    ASSIGN(lazy->first, first_val);
    if (rest_val && !IS_IMMEDIATE(rest_val))
        autorelease_pool_remove((CljObject*)rest_val);
    if (lazy->cached_rest != NOT_FOUND)
        RELEASE(lazy->cached_rest);
    lazy->cached_rest = rest_val;
    RELEASE(lazy->thunk);
    lazy->thunk = NULL;
}

static ID make_map_entry_vector(ID map_obj, int index) {
    CljPersistentMap *map = map_backing(map_obj);
    if (!map || index < 0 || index >= map->count) {
        return NULL;
    }

    CljObject *key = map->data[index * 2];
    CljObject *value = map->data[index * 2 + 1];

    CljPersistentVector *entry = make_vector(2, STRONG);
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

static int hashset_next_slot(const CljHashSet *set, int start) {
    if (!set) return -1;
    unsigned int i = start < 0 ? 0u : (unsigned int)start;
    for (; i < set->capacity; i++) {
        ID key = set->data[i];
        if (key != HASHSET_EMPTY && key != HASHSET_TOMBSTONE) {
            return (int)i;
        }
    }
    return -1;
}

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
        
        case CLJ_VECTOR_PERSISTENT:{
            CljPersistentVector *vec = as_persistent_vector(obj);

            // Initialize vector iterator using public API
            unsigned int count = vector_count(vec);
            if (count == 0) {
                return true;  // Empty vector
            }
            
            iter->state.vec.index = 0;
            iter->state.vec.count = count;
            iter->state.vec.data = NULL;  // Don't expose internal pointer
            iter->seq_type = CLJ_VECTOR_PERSISTENT;
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
            iter->state.str.data = string_data((ID)str);
            iter->state.str.index = 0;
            iter->state.str.length = string_length((ID)str);
            iter->seq_type = CLJ_STRING;
            return true;
        }
        
        case CLJ_MAP_PERSISTENT:
        case CLJ_MAP_TRANSIENT: {
            CljPersistentMap *map = map_backing(obj);
            if (map->count == 0) {
                return true;  // Empty map
            }

            iter->state.map.map = (struct CljPersistentMap *)map;
            iter->state.map.index = 0;
            iter->state.map.count = map->count;
            iter->seq_type = CLJ_MAP_PERSISTENT;
            iter->container = (CljObject*)map;
            return true;
        }
        case CLJ_HASHSET: {
            CljHashSet *set = (CljHashSet*)obj;
            if (!set || hashset_count(set) == 0) {
                return true;  // Empty set
            }
            int idx = hashset_next_slot(set, 0);
            if (idx < 0) {
                return true;  // No entries found
            }
            iter->state.hset.set = set;
            iter->state.hset.index = idx;
            iter->state.hset.capacity = (int)set->capacity;
            iter->seq_type = CLJ_HASHSET;
            iter->container = (CljObject*)set;
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
            if (first == NOT_FOUND) {
                // Still not realized - return NULL
                return NULL;
            }
            return (first == SYM_NIL) ? NULL : first;
        }
        
        case CLJ_VECTOR_PERSISTENT:
        case CLJ_VECTOR_TRANSIENT: {
            if (iter->state.vec.index < iter->state.vec.count) {
                // vector_nth returns element with lifetime tied to vector - no retain needed
                CljPersistentVector *vec = (CljPersistentVector*)iter->container;
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

        case CLJ_MAP_PERSISTENT: {
            if (iter->state.map.index < iter->state.map.count) {
                return make_map_entry_vector((ID)iter->state.map.map, iter->state.map.index);
            }
            return NULL;
        }
        case CLJ_HASHSET: {
            int idx = iter->state.hset.index;
            if (idx >= 0 && idx < iter->state.hset.capacity) {
                ID elem = iter->state.hset.set->data[idx];
                return (elem == SYM_NIL) ? NULL : elem;
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
                if (rest && is_list_type(TAG(rest))) {
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
        
        case CLJ_VECTOR_PERSISTENT:
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

        case CLJ_MAP_PERSISTENT: {
            if (iter->state.map.index < iter->state.map.count - 1) {
                iter->state.map.index++;
                return true;
            }
            iter->state.map.index = iter->state.map.count;
            return false;
        }
        
        case CLJ_HASHSET: {
            int next_idx = hashset_next_slot(iter->state.hset.set, iter->state.hset.index + 1);
            if (next_idx >= 0) {
                iter->state.hset.index = next_idx;
                return true;
            }
            iter->state.hset.index = iter->state.hset.capacity;
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
            case CLJ_VECTOR_PERSISTENT:
            case CLJ_VECTOR_TRANSIENT: {
                CljPersistentVector *vec = (CljPersistentVector*)iter->container;
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
        
        case CLJ_VECTOR_PERSISTENT:
        case CLJ_VECTOR_TRANSIENT:
            return iter->state.vec.index >= iter->state.vec.count;
        
        case CLJ_STRING:
            return iter->state.str.index >= iter->state.str.length;

        case CLJ_MAP_PERSISTENT:
            return iter->state.map.index >= iter->state.map.count;
        
        case CLJ_HASHSET:
            return iter->state.hset.index < 0 || iter->state.hset.index >= iter->state.hset.capacity;
        
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
        case CLJ_VECTOR_PERSISTENT:
        case CLJ_VECTOR_TRANSIENT:
            return iter->state.vec.index;
        case CLJ_STRING:
            return iter->state.str.index;
        case CLJ_MAP_PERSISTENT:
            return iter->state.map.index;
        case CLJ_HASHSET:
            return iter->state.hset.index;
        default:
            return 0;
    }
}

// ============================================================================
// COMPATIBILITY LAYER (Heap-based API)
// ============================================================================

bool collection_empty(ID obj) {
    if (!obj) return true;
    unsigned char t = TAG(obj);
    if (t == CLJ_VECTOR_PERSISTENT) {
        CljPersistentVector *v = as_persistent_vector(obj);
        return !v || vector_count(v) == 0;
    }
    if (is_list_type(t)) return list_empty(as_list((CljObject*)obj));
    if (t == CLJ_MAP_PERSISTENT || t == CLJ_MAP_TRANSIENT) {
        CljPersistentMap *m = as_map(obj);
        return !m || m->count == 0;
    }
    if (t == CLJ_HASHSET) {
        CljHashSet *s = (CljHashSet*)obj;
        return !s || hashset_count(s) == 0;
    }
    if (t == CLJ_STRING) return string_length(obj) == 0;
    if (t == CLJ_SEQ) return seq_empty(obj);
    return false;
}

/**
 * Return a sequence over obj, or NULL if nil/empty. Non-NULL return is always
 * caller-owned (new with rc=1 or RETAIN(seq)); must be released or autoreleased.
 * @param obj  Collection or nil
 * @return     New seq wrapper, or RETAIN(seq) if obj already CLJ_SEQ, or NULL
 */
CljSeqIterator* make_seq(ID obj) {
    if (!obj) return NULL;
    unsigned char obj_tag = TAG(obj);
    if (obj_tag == CLJ_SEQ) return (CljSeqIterator*)RETAIN(obj);
    if (collection_empty(obj)) return NULL;

    SeqIterator stack_iter;
    if (!seq_iter_init(&stack_iter, (CljObject*)obj) || seq_iter_empty(&stack_iter))
        return NULL;

    CljSeqIterator *heap_seq = ALLOC(CljSeqIterator, 1);
    if (!heap_seq) throw_oom();
    heap_seq->base.type = CLJ_SEQ;
    heap_seq->base.rc = 1;
    heap_seq->iter = stack_iter;
    RETAIN(obj);
    return heap_seq;
}

void seq_release(ID seq_obj) {
    if (!seq_obj) return;
    CljSeqIterator *seq = as_seq(seq_obj);
    if (!seq) return;
    if (seq->iter.container)
        RELEASE(seq->iter.container);
    CLJ_FREE(seq);
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
    CljSeqIterator *rest_seq = ALLOC(CljSeqIterator, 1);
    if (!rest_seq) return NULL;
    
    rest_seq->base.type = CLJ_SEQ;
    rest_seq->base.rc = 1;
    rest_seq->iter = seq->iter;
    seq_iter_next(&rest_seq->iter);
    if (rest_seq->iter.container)
        RETAIN(rest_seq->iter.container);
    return (CljObject*)rest_seq;
}

/**
 * @brief Advance seq to next element; for CLJ_LIST returns the list rest directly.
 * @param seq_obj Current seq (or list when over a list).
 * @return Next seq/rest, or NULL if empty. Caller-owned when a new seq is
 *         allocated; for list, the returned rest is part of the original list.
 * @note If the caller will release the head (the original seq or its container),
 *       they must RETAIN(seq_next(...)) before releasing. Using
 *       AUTORELEASE(RETAIN(result)) would be safer but would poison COW optimizations.
 */
ID seq_next(ID seq_obj) {
    if (!seq_obj) return NULL;
    /* If the original sequence was a CLJ_LIST, return CLJ_LIST directly (native_next semantics). */
    CljSeqIterator *seq = as_seq(seq_obj);
    if (seq && seq->iter.seq_type == CLJ_LIST) {
        if (seq->iter.state.list.current) {
            CljList *current_list = as_list(seq->iter.state.list.current);
            if (current_list) {
                CljObject *rest = LIST_REST(current_list);
                return rest;
            }
        }
        return NULL;
    }

    // For other types (CLJ_VECTOR_PERSISTENT, CLJ_STRING, etc.), use seq_rest
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

/**
 * COW optimization: advance the sequence in the slot. When the iterator is exclusive (rc==1)
 * and not a list, advance in-place; otherwise ASSIGN(*seq_slot, next). Callers do not need
 * to implement this copy-on-write / in-place logic themselves.
 */
void seq_next_inplace(ID *seq_slot) {
    if (!seq_slot || !*seq_slot) return;
    CljSeqIterator *seq = as_seq(*seq_slot);
    if (!seq) {
        /* Not a heap seq (e.g. list passed as seq): advance via seq_next and update slot. */
        ID next = seq_next(*seq_slot);
        RETAIN(next);
        RELEASE(*seq_slot);
        *seq_slot = next;
        return;
    }
    if (seq->iter.seq_type == CLJ_LIST || seq->base.rc != 1) {
        ID next = seq_next(*seq_slot);
        RETAIN(next);
        RELEASE(*seq_slot);
        *seq_slot = next;
        return;
    }
    if (!seq_iter_next(&seq->iter)) {
        RELEASE(*seq_slot);
        *seq_slot = NULL;
        return;
    }
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
            case CLJ_VECTOR_PERSISTENT:
            case CLJ_VECTOR_TRANSIENT:
                // Return remaining elements, not total count
                return seq->iter.state.vec.count - seq->iter.state.vec.index;
            case CLJ_LIST:
                // List doesn't have direct count in state, fall through to iterate
                break;
            case CLJ_STRING:
                // Return remaining characters, not total length
                return seq->iter.state.str.length - seq->iter.state.str.index;
            case CLJ_MAP_PERSISTENT:
                // Return remaining entries, not total count
                return seq->iter.state.map.count - seq->iter.state.map.index;
            case CLJ_HASHSET: {
                CljHashSet *set = seq->iter.state.hset.set;
                int remaining = 0;
                if (set) {
                    for (int i = seq->iter.state.hset.index; i < seq->iter.state.hset.capacity; i++) {
                        ID key = set->data[i];
                        if (key != HASHSET_EMPTY && key != HASHSET_TOMBSTONE) {
                            remaining++;
                        }
                    }
                }
                return remaining;
            }
            default:
                return 0;
        }
    }
    
    // Fast path for vectors - O(1)
    if (TAG(obj) == CLJ_VECTOR_PERSISTENT) {
        CljPersistentVector *vec = as_persistent_vector(obj);
        return vec ? (int)vector_count(vec) : 0;
    }

    if (TAG(obj) == CLJ_HASHSET) {
        return (int)hashset_count((CljHashSet*)obj);
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
        case CLJ_VECTOR_PERSISTENT:
        case CLJ_VECTOR_TRANSIENT:
        case CLJ_MAP_PERSISTENT:
        case CLJ_HASHSET:
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
    if (is_list_type(TAG(obj))) {
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
        RELEASE(lazy_seq->thunk_state);
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
