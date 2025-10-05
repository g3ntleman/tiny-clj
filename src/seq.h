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
    CljObject *container;    // Original container (borrowed reference)
    void *state;             // Iterator-specific state
    int position;            // Current position in sequence
    CljType seq_type;        // Type of sequence (for dispatch)
} SeqIterator;

/**
 * @brief Create a sequence view from any seqable container
 * @param obj Container (list, vector, map, string, nil)
 * @return Iterator view, or NULL if not seqable
 */
SeqIterator* seq_create(CljObject *obj);

/**
 * @brief Get the first element of a sequence
 * @param seq Sequence iterator
 * @return First element, or nil if empty
 */
CljObject* seq_first(SeqIterator *seq);

/**
 * @brief Get the rest of a sequence (without first element)
 * @param seq Sequence iterator
 * @return New iterator for rest, or NULL if empty
 */
SeqIterator* seq_rest(SeqIterator *seq);

/**
 * @brief Get the next element of a sequence (alias for rest)
 * @param seq Sequence iterator
 * @return New iterator for next, or NULL if empty
 */
SeqIterator* seq_next(SeqIterator *seq);

/**
 * @brief Check if sequence is empty
 * @param seq Sequence iterator
 * @return true if empty, false otherwise
 */
bool seq_empty(SeqIterator *seq);

/**
 * @brief Get the nth element of a sequence
 * @param seq Sequence iterator
 * @param n Index (0-based)
 * @return nth element, or nil if out of bounds
 */
CljObject* seq_nth(SeqIterator *seq, int n);

/**
 * @brief Get the count of a sequence
 * @param seq Sequence iterator
 * @return Number of elements, or -1 if not countable
 */
int seq_count(SeqIterator *seq);

/**
 * @brief Release a sequence iterator
 * @param seq Iterator to release
 */
void seq_release(SeqIterator *seq);

/**
 * @brief Retain a sequence iterator (for long-lived views)
 * @param seq Iterator to retain
 */
void seq_retain(SeqIterator *seq);

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
CljObject* seq_reduce(SeqIterator *seq, CljObject *f, CljObject *init);

/**
 * @brief Map over a sequence
 * @param seq Sequence iterator
 * @param f Mapping function (f item) -> new_item
 * @return New list with mapped values
 */
CljObject* seq_map(SeqIterator *seq, CljObject *f);

/**
 * @brief Filter a sequence
 * @param seq Sequence iterator
 * @param pred Predicate function (pred item) -> boolean
 * @return New list with filtered values
 */
CljObject* seq_filter(SeqIterator *seq, CljObject *pred);

/**
 * @brief Take n elements from a sequence
 * @param seq Sequence iterator
 * @param n Number of elements to take
 * @return New list with first n elements
 */
CljObject* seq_take(SeqIterator *seq, int n);

/**
 * @brief Drop n elements from a sequence
 * @param seq Sequence iterator
 * @param n Number of elements to drop
 * @return New iterator starting after n elements
 */
SeqIterator* seq_drop(SeqIterator *seq, int n);

// ============================================================================
// SEQUENCE EQUALITY
// ============================================================================

/**
 * @brief Compare two sequences for equality
 * @param seq1 First sequence
 * @param seq2 Second sequence
 * @return true if equal, false otherwise
 */
bool seq_equal(SeqIterator *seq1, SeqIterator *seq2);

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
CljObject* seq_to_list(SeqIterator *seq);

/**
 * @brief Convert a sequence to a vector
 * @param seq Sequence iterator
 * @return New vector containing all elements
 */
CljObject* seq_to_vector(SeqIterator *seq);

/**
 * @brief Concatenate multiple sequences
 * @param sequences Array of sequence iterators
 * @param count Number of sequences
 * @return New list containing all elements
 */
CljObject* seq_concat(SeqIterator **sequences, int count);

#ifdef __cplusplus
}
#endif

#endif /* TINY_CLJ_SEQ_H */
