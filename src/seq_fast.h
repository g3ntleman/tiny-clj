/*
 * Fast Seq Implementation for Tiny-CLJ
 * 
 * Stack-allocated iterator with zero-copy semantics for vectors.
 * Reduces heap allocation overhead from 61x to ~5x.
 */

#ifndef TINY_CLJ_SEQ_FAST_H
#define TINY_CLJ_SEQ_FAST_H

#include "CljObject.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// FAST SEQ INTERFACE (Stack-Allocated)
// ============================================================================

/**
 * @brief Fast iterator-based sequence (stack-allocated)
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
} FastSeqIterator;

/**
 * @brief Initialize a fast seq iterator (stack-allocated)
 * @param iter Stack-allocated iterator to initialize
 * @param obj Container to iterate over
 * @return true if successful, false if not seqable
 */
bool fast_seq_init(FastSeqIterator *iter, CljObject *obj);

/**
 * @brief Get the first element of a sequence
 * @param iter Iterator
 * @return First element, or nil if empty
 */
CljObject* fast_seq_first(const FastSeqIterator *iter);

/**
 * @brief Advance iterator to next element (in-place mutation)
 * @param iter Iterator to advance
 * @return true if more elements, false if exhausted
 */
bool fast_seq_next(FastSeqIterator *iter);

/**
 * @brief Check if sequence is empty
 * @param iter Iterator
 * @return true if empty, false otherwise
 */
bool fast_seq_empty(const FastSeqIterator *iter);

/**
 * @brief Get current position in sequence
 * @param iter Iterator
 * @return Current index
 */
int fast_seq_position(const FastSeqIterator *iter);

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

/**
 * @brief Iterate over a seqable collection (stack-allocated, zero-copy)
 * 
 * Usage:
 *   FAST_SEQ_FOREACH(vec, item) {
 *       // use item
 *   }
 */
#define FAST_SEQ_FOREACH(container, item_var) \
    FastSeqIterator _iter; \
    if (fast_seq_init(&_iter, (container))) \
        for (CljObject *item_var = fast_seq_first(&_iter); \
             !fast_seq_empty(&_iter); \
             fast_seq_next(&_iter), item_var = fast_seq_first(&_iter))

/**
 * @brief Count elements in a seqable collection (optimized)
 */
static inline int fast_seq_count(CljObject *obj) {
    if (!obj) return 0;
    
    // Fast path for vectors - O(1)
    if (obj->type == CLJ_VECTOR) {
        CljPersistentVector *vec = as_vector(obj);
        return vec ? vec->count : 0;
    }
    
    // Fallback: iterate and count - O(n)
    FastSeqIterator iter;
    if (!fast_seq_init(&iter, obj)) return 0;
    
    int count = 0;
    while (!fast_seq_empty(&iter)) {
        count++;
        fast_seq_next(&iter);
    }
    return count;
}

#ifdef __cplusplus
}
#endif

#endif /* TINY_CLJ_SEQ_FAST_H */

