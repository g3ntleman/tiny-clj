#ifndef LOCKFREE_SPSC_QUEUE_H
#define LOCKFREE_SPSC_QUEUE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Bounded lock-free single-producer/single-consumer ring buffer.
 *
 * - Caller owns the backing storage and element type.
 * - `capacity` is the number of elements in `storage`.
 * - `elem_size` is the size of one element in bytes.
 * - Queue is SPSC only (exactly one producer thread and one consumer thread).
 */
typedef struct {
    uint8_t         *storage;
    uint32_t         capacity;
    /* Fast path for power-of-two capacities (common on embedded ring buffers):
     * when != UINT32_MAX, slot index uses `idx & capacity_mask` instead of `%`.
     */
    uint32_t         capacity_mask;
    size_t           elem_size;
    _Atomic uint32_t write;
    _Atomic uint32_t read;
} LockFreeSpscQueue;

bool lockfree_spsc_queue_init(LockFreeSpscQueue *q, void *storage, uint32_t capacity, size_t elem_size);
void lockfree_spsc_queue_reset(LockFreeSpscQueue *q);

bool lockfree_spsc_queue_push(LockFreeSpscQueue *q, const void *elem);
bool lockfree_spsc_queue_pop(LockFreeSpscQueue *q, void *out);

bool lockfree_spsc_queue_empty(LockFreeSpscQueue *q);
bool lockfree_spsc_queue_full(LockFreeSpscQueue *q);
uint32_t lockfree_spsc_queue_count(LockFreeSpscQueue *q);

#endif
