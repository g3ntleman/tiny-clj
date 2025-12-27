/*
 * Seq Implementation for Tiny-CLJ
 * 
 * Stack-allocated iterator with zero-copy semantics for vectors.
 * Optimized for embedded systems (2.3x faster than heap-based version).
 */

#ifndef TINY_CLJ_SEQ_H
#define TINY_CLJ_SEQ_H

#include "object.h"
#include <stdbool.h>

struct CljMap;  // Forward declaration to avoid including map.h here

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
            struct CljMap *map;  // Map being iterated
            int index;           // Current entry index
            int count;           // Total entries
        } map;
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

/**
 * @brief Lazy sequence structure
 * Clojure-compatible lazy sequence with generator function
 */
typedef struct {
    CljObject base;
    ID first;
    ID rest_fn;
    ID cached_rest;
} CljLazySeq;

/**
 * @brief Create heap-allocated seq (legacy compatibility)
 */
CljSeqIterator* make_seq(ID obj);

CljLazySeq* make_lazy_seq(ID first, ID rest_fn);

static inline bool is_lazy_seq(ID seq) {
    return seq && TAG(seq) == CLJ_LAZY_SEQ;
}

static inline CljLazySeq* as_lazy_seq(ID obj) {
    return (TAG(obj) == CLJ_LAZY_SEQ) ? (CljLazySeq*)obj : NULL;
}

/**
 * @brief Heap-based seq API (legacy compatibility, uses stack implementation internally)
 */
ID seq_first(ID seq);
ID seq_rest(ID seq);
ID seq_next(ID seq);
ID seq_next_inplace(ID seq);
bool seq_empty(ID seq);
int seq_count(ID obj);
void seq_release(ID seq_obj);

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

#ifdef __cplusplus
}
#endif

#endif /* TINY_CLJ_SEQ_H */

