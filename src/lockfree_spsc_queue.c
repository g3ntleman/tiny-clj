#include "lockfree_spsc_queue.h"

#include <string.h>

#include "common.h"

static inline uint8_t *lockfree_spsc_queue_slot_ptr(LockFreeSpscQueue *q, uint32_t index) {
    CLJ_ASSERT(q != NULL);
    CLJ_ASSERT(q->storage != NULL);
    CLJ_ASSERT(q->capacity > 0);
    uint32_t slot_index = 0;
    if (q->capacity_mask != UINT32_MAX) {
        slot_index = index & q->capacity_mask;
    } else {
        slot_index = index % q->capacity;
    }
    return q->storage + ((size_t)slot_index * q->elem_size);
}

bool lockfree_spsc_queue_init(LockFreeSpscQueue *q, void *storage, uint32_t capacity, size_t elem_size) {
    if (!q || !storage || capacity == 0 || elem_size == 0) return false;
    /* Keep (write - read) arithmetic unambiguous across uint32_t wraparound. */
    if (capacity >= 0x80000000u) return false;

    q->storage = (uint8_t *)storage;
    q->capacity = capacity;
    q->capacity_mask = ((capacity & (capacity - 1u)) == 0u) ? (capacity - 1u) : UINT32_MAX;
    q->elem_size = elem_size;
    atomic_store_explicit(&q->write, 0u, memory_order_relaxed);
    atomic_store_explicit(&q->read, 0u, memory_order_relaxed);
    return true;
}

void lockfree_spsc_queue_reset(LockFreeSpscQueue *q) {
    CLJ_ASSERT(q != NULL);
    atomic_store_explicit(&q->write, 0u, memory_order_relaxed);
    atomic_store_explicit(&q->read, 0u, memory_order_relaxed);
}

bool lockfree_spsc_queue_push(LockFreeSpscQueue *q, const void *elem) {
    CLJ_ASSERT(q != NULL);
    CLJ_ASSERT(elem != NULL);
    if (!q || !elem) return false;

    uint32_t w = atomic_load_explicit(&q->write, memory_order_relaxed);
    uint32_t r = atomic_load_explicit(&q->read, memory_order_acquire);
    if ((w - r) >= q->capacity) return false;

    memcpy(lockfree_spsc_queue_slot_ptr(q, w), elem, q->elem_size);
    atomic_store_explicit(&q->write, w + 1u, memory_order_release);
    return true;
}

bool lockfree_spsc_queue_pop(LockFreeSpscQueue *q, void *out) {
    CLJ_ASSERT(q != NULL);
    CLJ_ASSERT(out != NULL);
    if (!q || !out) return false;

    uint32_t r = atomic_load_explicit(&q->read, memory_order_relaxed);
    uint32_t w = atomic_load_explicit(&q->write, memory_order_acquire);
    if (r == w) return false;

    memcpy(out, lockfree_spsc_queue_slot_ptr(q, r), q->elem_size);
    atomic_store_explicit(&q->read, r + 1u, memory_order_release);
    return true;
}

bool lockfree_spsc_queue_empty(LockFreeSpscQueue *q) {
    CLJ_ASSERT(q != NULL);
    if (!q) return true;

    uint32_t r = atomic_load_explicit(&q->read, memory_order_relaxed);
    uint32_t w = atomic_load_explicit(&q->write, memory_order_acquire);
    return r == w;
}

bool lockfree_spsc_queue_full(LockFreeSpscQueue *q) {
    CLJ_ASSERT(q != NULL);
    if (!q) return false;

    uint32_t w = atomic_load_explicit(&q->write, memory_order_relaxed);
    uint32_t r = atomic_load_explicit(&q->read, memory_order_acquire);
    return (w - r) >= q->capacity;
}

uint32_t lockfree_spsc_queue_count(LockFreeSpscQueue *q) {
    CLJ_ASSERT(q != NULL);
    if (!q) return 0u;

    uint32_t r = atomic_load_explicit(&q->read, memory_order_relaxed);
    uint32_t w = atomic_load_explicit(&q->write, memory_order_acquire);
    return w - r;
}
