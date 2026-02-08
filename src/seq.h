/*
 * Seq Implementation for Tiny-CLJ
 * 
 * Stack-allocated iterator with zero-copy semantics for vectors.
 * Optimized for embedded systems (2.3x faster than heap-based version).
 */

#ifndef TINY_CLJ_SEQ_H
#define TINY_CLJ_SEQ_H

#include "common.h"  // For CLJ_ASSERT
#include "object.h"
#include <stdbool.h>

struct CljPersistentMap;  // Forward declaration to avoid including map.h here
struct CljHashSet;        // Forward declaration to avoid including hashset.h here

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// SEQ INTERFACE (Stack-Allocated)
// ============================================================================

/**
 * @brief Iterator-based sequence (stack-allocated)
 * 
 * This is a lightweight iterator that can be allocated on the stack.
 * No heap allocation required for iteration!
 */
typedef struct {
    CljObject *container;    // Original container (borrowed reference)
    union {
        struct {
            CljObject **data;    // Direct pointer to vector data
            int index;           // Current position
            int count;           // Total elements
        } vec;
        struct {
            CljObject *current;  // Current list node
            int index;           // Current position
        } list;
        struct {
            const char *data;    // String data
            int index;           // Current character position
            int length;          // Total length
        } str;
        struct {
            struct CljPersistentMap *map;  // Map being iterated
            int index;           // Current entry index
            int count;           // Total entries
        } map;
        struct {
            struct CljHashSet *set;  // Set being iterated
            int index;           // Current slot index
            int capacity;        // Total slots
        } hset;
    } state;
    CljType seq_type;        // Type of sequence (for dispatch)
} SeqIterator;

/**
 * @brief Initialize a seq iterator (stack-allocated)
 * @param iter Stack-allocated iterator to initialize
 * @param obj Container to iterate over
 * @return true if successful, false if not seqable
 */
bool seq_iter_init(SeqIterator *iter, ID obj);

/**
 * @brief Get the first element of a sequence
 * @param iter Iterator
 * @return First element, or nil if empty
 */
ID seq_iter_first(const SeqIterator *iter);

/**
 * @brief Advance iterator to next element (in-place mutation)
 * @param iter Iterator to advance
 * @return true if more elements, false if exhausted
 */
bool seq_iter_next(SeqIterator *iter);

/**
 * @brief Check if sequence is empty
 * @param iter Iterator
 * @return true if empty, false otherwise
 */
bool seq_iter_empty(const SeqIterator *iter);

/**
 * @brief Get current position in sequence
 * @param iter Iterator
 * @return Current index
 */
int seq_iter_position(const SeqIterator *iter);

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

/**
 * @brief Iterate over a seqable collection (stack-allocated, zero-copy)
 * 
 * Usage:
 *   SEQ_FOREACH(vec, item) {
 *       // use item
 *   }
 */
#define SEQ_FOREACH(container, item_var) \
    SeqIterator _iter; \
    if (seq_iter_init(&_iter, (container))) \
        for (CljObject *item_var = seq_iter_first(&_iter); \
             !seq_iter_empty(&_iter); \
             seq_iter_next(&_iter), item_var = seq_iter_first(&_iter))

/**
 * @brief Count elements in a seqable collection (optimized)
 */
int seq_count(ID obj);

// ============================================================================
// COMPATIBILITY LAYER (Heap-based API using stack implementation)
// ============================================================================

/**
 * @brief Heap-allocated seq wrapper for compatibility
 * This wraps SeqIterator in a heap object for legacy code compatibility
 */
typedef struct {
    CljObject base;         // Base object (CLJ_SEQ type)
    SeqIterator iter;       // Embedded stack iterator
} CljSeqIterator;

// ============================================================================
// LAZY SEQ (Heap-based)
// ============================================================================

/**
 * @brief Heap-allocated lazy sequence.
 *
 * LazySeq realization model:
 * - Before realization: first == NOT_FOUND and thunk is a 0-arity thunk that
 *   produces a seqable value.
 * - After realization: first holds the first element (may be NULL for a nil
 *   element), cached_rest holds the rest sequence, and thunk is released.
 */
typedef struct {
    CljObject base;     // Base object (CLJ_LAZY_SEQ type)
    ID first;           // NOT_FOUND until realized; then first element (may be NULL)
    ID thunk;           // 0-arity or 1-arity thunk (released after realization)
    ID cached_rest;     // NOT_FOUND until realized; then rest sequence (may be NULL for empty)
    ID thunk_state;     // optional: state map passed as single arg when realizing (embedded thunks)
} CljLazySeq;

/**
 * @brief Create a lazy sequence backed by a 0-arity thunk.
 */
CljLazySeq* make_lazy_seq(ID thunk);

/**
 * @brief Create heap-allocated seq (legacy compatibility)
 */
CljSeqIterator* make_seq(ID obj);

/**
 * @brief Heap-based seq API (legacy compatibility, uses stack implementation internally)
 */
ID seq_first(ID seq);
ID seq_rest(ID seq);
ID seq_next(ID seq);
ID seq_next_inplace(ID seq);
bool seq_empty(ID seq);
int seq_count(ID obj);

/**
 * @brief Seqable predicates
 */
bool is_seqable(ID obj);
bool is_seq(ID obj);

/**
 * @brief Cast to CljSeqIterator (legacy compatibility)
 */
static inline CljSeqIterator* as_seq(ID obj) {
    return (TAG((CljObject*)obj) == CLJ_SEQ) ? (CljSeqIterator*)obj : NULL;
}

/**
 * @brief Cast to CljLazySeq.
 */
static inline CljLazySeq* as_lazy_seq(ID obj) {
    return (TAG((CljObject*)obj) == CLJ_LAZY_SEQ) ? (CljLazySeq*)obj : NULL;
}

static inline bool is_lazy_seq(ID obj) {
    return TAG((CljObject*)obj) == CLJ_LAZY_SEQ;
}

/**
 * @brief Register seq-related release handlers with subjective-c memory system.
 * 
 * Should be called during runtime initialization.
 */
void seq_register_release_fn(void);

#ifdef __cplusplus
}
#endif

#endif /* TINY_CLJ_SEQ_H */
