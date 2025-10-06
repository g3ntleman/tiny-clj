/*
 * Seq Semantics for Tiny-CLJ
 * 
 * Implements Clojure's seq abstraction with iterator-based views
 * for efficient traversal without heap allocation.
 */

#ifndef TINY_CLJ_SEQ_H
#define TINY_CLJ_SEQ_H

#include "CljObject.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// SEQ INTERFACE
// ============================================================================

/**
 * @brief Iterator-based sequence view
 * 
 * Borrowed view pattern: the container must outlive the iterator.
 * For long-lived iterators, call seq_retain() to extend lifetime.
 */
typedef struct {
    CljObject base;         // Embedded base object (CLJ_SEQ type)
    CljObject *container;    // Original container (borrowed reference)
    void *state;             // Iterator-specific state
    int position;            // Current position in sequence
    CljType seq_type;        // Type of sequence (for dispatch)
} CljSeqIterator;

/**
 * @brief Create a sequence view from any seqable container
 * @param obj Container (list, vector, map, string, nil)
 * @return Iterator view as CljObject, or NULL if not seqable
 */
CljObject* seq_create(CljObject *obj);

/**
 * @brief Cast CljObject to CljSeqIterator
 * @param obj Object to cast
 * @return CljSeqIterator* if obj is CLJ_SEQ type, NULL otherwise
 */
static inline CljSeqIterator* as_seq(CljObject *obj) {
    return (type(obj) == CLJ_SEQ) ? (CljSeqIterator*)obj : NULL;
}

/**
 * @brief Get the first element of a sequence
 * @param seq Sequence iterator
 * @return First element, or nil if empty
 */
CljObject* seq_first(CljObject *seq);

// Direct seq operations - lists and sequences are the same thing

/**
 * @brief Get the rest of a sequence (without first element)
 * @param seq Sequence iterator
 * @return New iterator for rest, or NULL if empty
 */
CljObject* seq_rest(CljObject *seq);

/**
 * @brief Get the next element of a sequence (alias for rest)
 * @param seq Sequence iterator
 * @return New iterator for next, or NULL if empty
 */
CljObject* seq_next(CljObject *seq);

/**
 * @brief Check if sequence is empty
 * @param seq Sequence iterator
 * @return true if empty, false otherwise
 */
bool seq_empty(CljObject *seq);

/**
 * @brief Get the nth element of a sequence
 * @param seq Sequence iterator
 * @param n Index (0-based)
 * @return nth element, or nil if out of bounds
 */
CljObject* seq_nth(CljObject *seq, int n);

/**
 * @brief Get the count of a sequence
 * @param seq Sequence iterator
 * @return Number of elements, or -1 if not countable
 */
int seq_count(CljObject *seq);

/**
 * @brief Release a sequence iterator
 * @param seq Iterator to release
 */
void seq_release(CljObject *seq);

/**
 * @brief Retain a sequence iterator (for long-lived views)
 * @param seq Iterator to retain
 */
void seq_retain(CljObject *seq);

// ============================================================================
// SEQABLE PREDICATES
// ============================================================================

/**
 * @brief Check if an object is seqable
 * @param obj Object to check
 * @return true if seqable, false otherwise
 */
bool is_seqable(CljObject *obj);

/**
 * @brief Check if an object is a sequence
 * @param obj Object to check
 * @return true if already a sequence, false otherwise
 */
bool is_seq(CljObject *obj);

// ============================================================================
// HIGHER-ORDER SEQ FUNCTIONS
// ============================================================================

/**
 * @brief Reduce over a sequence
 * @param seq Sequence iterator
 * @param f Reduction function (f accumulator item) -> new_accumulator
 * @param init Initial value
 * @return Final reduced value
 */
CljObject* seq_reduce(CljObject *seq, CljObject *f, CljObject *init);

/**
 * @brief Map over a sequence
 * @param seq Sequence iterator
 * @param f Mapping function (f item) -> new_item
 * @return New list with mapped values
 */
CljObject* seq_map(CljObject *seq, CljObject *f);

/**
 * @brief Filter a sequence
 * @param seq Sequence iterator
 * @param pred Predicate function (pred item) -> boolean
 * @return New list with filtered values
 */
CljObject* seq_filter(CljObject *seq, CljObject *pred);

/**
 * @brief Take n elements from a sequence
 * @param seq Sequence iterator
 * @param n Number of elements to take
 * @return New list with first n elements
 */
CljObject* seq_take(CljObject *seq, int n);

/**
 * @brief Drop n elements from a sequence
 * @param seq Sequence iterator
 * @param n Number of elements to drop
 * @return New iterator starting after n elements
 */
CljObject* seq_drop(CljObject *seq, int n);

// ============================================================================
// SEQUENCE EQUALITY
// ============================================================================

/**
 * @brief Compare two sequences for equality
 * @param seq1 First sequence
 * @param seq2 Second sequence
 * @return true if equal, false otherwise
 */
bool seq_equal(CljObject *seq1, CljObject *seq2);

/**
 * @brief Compare two seqable objects for equality
 * @param obj1 First object
 * @param obj2 Second object
 * @return true if equal when sequenced, false otherwise
 */
bool seqable_equal(CljObject *obj1, CljObject *obj2);

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * @brief Convert a sequence to a list
 * @param seq Sequence iterator
 * @return New list containing all elements
 */
CljObject* seq_to_list(CljObject *seq);

/**
 * @brief Convert a sequence to a vector
 * @param seq Sequence iterator
 * @return New vector containing all elements
 */
CljObject* seq_to_vector(CljObject *seq);

/**
 * @brief Concatenate multiple sequences
 * @param sequences Array of sequence iterators
 * @param count Number of sequences
 * @return New list containing all elements
 */
CljObject* seq_concat(CljObject **sequences, int count);

#ifdef __cplusplus
}
#endif

#endif /* TINY_CLJ_SEQ_H */
