/*
 * Optimized Seq Implementation for Tiny-CLJ
 * 
 * Provides high-performance seq iteration with iterator pooling
 * and direct state manipulation to reduce overhead.
 */

#ifndef TINY_CLJ_SEQ_OPTIMIZED_H
#define TINY_CLJ_SEQ_OPTIMIZED_H

#include "CljObject.h"
#include "seq.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// ITERATOR POOL
// ============================================================================

#define SEQ_POOL_SIZE 64

typedef struct {
    SeqIterator pool[SEQ_POOL_SIZE];
    bool in_use[SEQ_POOL_SIZE];
    int next_free;
} SeqIteratorPool;

// Global iterator pool
extern SeqIteratorPool g_seq_pool;

/**
 * @brief Initialize the global iterator pool
 */
void seq_pool_init(void);

/**
 * @brief Get a pooled iterator (reuse existing or create new)
 * @param obj Container to iterate over
 * @return Pooled iterator, or NULL if pool exhausted
 */
SeqIterator* seq_pool_get(CljObject *obj);

/**
 * @brief Return an iterator to the pool
 * @param seq Iterator to return
 */
void seq_pool_return(SeqIterator *seq);

/**
 * @brief Advance an iterator in-place (no new allocation)
 * @param seq Iterator to advance
 * @return true if advanced, false if at end
 */
bool seq_advance_inplace(SeqIterator *seq);

// ============================================================================
// HIGH-PERFORMANCE SEQ OPERATIONS
// ============================================================================

/**
 * @brief High-performance seq iteration with pooled iterators
 * @param obj Container to iterate over
 * @param callback Function to call for each element
 * @param user_data User data passed to callback
 * @return Number of elements processed
 */
int seq_iterate_fast(CljObject *obj, void (*callback)(CljObject*, void*), void *user_data);

/**
 * @brief Fast seq iteration with early termination
 * @param obj Container to iterate over
 * @param callback Function to call for each element (return false to stop)
 * @param user_data User data passed to callback
 * @return Number of elements processed
 */
int seq_iterate_until(CljObject *obj, bool (*callback)(CljObject*, void*), void *user_data);

/**
 * @brief Bulk seq operations (process multiple elements at once)
 * @param obj Container to iterate over
 * @param batch_size Number of elements to process at once
 * @param callback Function to call for each batch
 * @param user_data User data passed to callback
 * @return Number of batches processed
 */
int seq_iterate_batch(CljObject *obj, int batch_size, void (*callback)(CljObject**, int, void*), void *user_data);

// ============================================================================
// TYPE-SPECIFIC OPTIMIZATIONS
// ============================================================================

/**
 * @brief Fast vector iteration (direct access when possible)
 * @param vec Vector to iterate over
 * @param callback Function to call for each element
 * @param user_data User data passed to callback
 * @return Number of elements processed
 */
int vector_iterate_fast(CljObject *vec, void (*callback)(CljObject*, void*), void *user_data);

/**
 * @brief Fast list iteration (direct traversal)
 * @param list List to iterate over
 * @param callback Function to call for each element
 * @param user_data User data passed to callback
 * @return Number of elements processed
 */
int list_iterate_fast(CljObject *list, void (*callback)(CljObject*, void*), void *user_data);

/**
 * @brief Fast string iteration (character by character)
 * @param str String to iterate over
 * @param callback Function to call for each character
 * @param user_data User data passed to callback
 * @return Number of characters processed
 */
int string_iterate_fast(CljObject *str, void (*callback)(char, void*), void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* TINY_CLJ_SEQ_OPTIMIZED_H */
