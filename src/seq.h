/*
 * Seq Implementation for Tiny-CLJ
 * 
 * Stack-allocated iterator with zero-copy semantics for vectors.
 * Optimized for embedded systems (2.3x faster than heap-based version).
 */

#ifndef TINY_CLJ_SEQ_H
#define TINY_CLJ_SEQ_H

#include "CljObject.h"
#include <stdbool.h>

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
    } state;
    CljType seq_type;        // Type of sequence (for dispatch)
} SeqIterator;

/**
 * @brief Initialize a seq iterator (stack-allocated)
 * @param iter Stack-allocated iterator to initialize
 * @param obj Container to iterate over
 * @return true if successful, false if not seqable
 */
bool seq_iter_init(SeqIterator *iter, CljObject *obj);

/**
 * @brief Get the first element of a sequence
 * @param iter Iterator
 * @return First element, or nil if empty
 */
CljObject* seq_iter_first(const SeqIterator *iter);

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
int seq_count(CljObject *obj);

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
 * @brief Create heap-allocated seq (legacy compatibility)
 */
CljObject* seq_create(CljObject *obj);

/**
 * @brief Heap-based seq API (legacy compatibility, uses stack implementation internally)
 */
CljObject* seq_first(CljObject *seq);
CljObject* seq_rest(CljObject *seq);
CljObject* seq_next(CljObject *seq);
bool seq_empty(CljObject *seq);
int seq_count(CljObject *obj);
void seq_release(CljObject *seq);

/**
 * @brief Seqable predicates
 */
bool is_seqable(CljObject *obj);
bool is_seq(CljObject *obj);

/**
 * @brief Cast to CljSeqIterator (legacy compatibility)
 */
static inline CljSeqIterator* as_seq(CljObject *obj) {
    return (type(obj) == CLJ_SEQ) ? (CljSeqIterator*)obj : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* TINY_CLJ_SEQ_H */

