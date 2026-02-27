#include "tests_common.h"

#include "../lockfree_spsc_queue.h"

typedef struct {
    int32_t a;
    int32_t b;
} QueuePair;

TEST(test_lockfree_spsc_queue_push_pop) {
    QueuePair storage[4];
    LockFreeSpscQueue q;
    TEST_ASSERT_TRUE(lockfree_spsc_queue_init(&q, storage, 4u, sizeof(storage[0])));

    QueuePair in = {.a = 11, .b = 22};
    TEST_ASSERT_TRUE(lockfree_spsc_queue_push(&q, &in));
    TEST_ASSERT_FALSE(lockfree_spsc_queue_empty(&q));
    TEST_ASSERT_EQUAL_UINT32(1u, lockfree_spsc_queue_count(&q));

    QueuePair out = {0};
    TEST_ASSERT_TRUE(lockfree_spsc_queue_pop(&q, &out));
    TEST_ASSERT_EQUAL_INT32(11, out.a);
    TEST_ASSERT_EQUAL_INT32(22, out.b);
    TEST_ASSERT_TRUE(lockfree_spsc_queue_empty(&q));
    TEST_ASSERT_EQUAL_UINT32(0u, lockfree_spsc_queue_count(&q));
}

TEST(test_lockfree_spsc_queue_full) {
    QueuePair storage[3];
    LockFreeSpscQueue q;
    TEST_ASSERT_TRUE(lockfree_spsc_queue_init(&q, storage, 3u, sizeof(storage[0])));

    for (int i = 0; i < 3; i++) {
        QueuePair in = {.a = i, .b = -i};
        TEST_ASSERT_TRUE(lockfree_spsc_queue_push(&q, &in));
    }

    TEST_ASSERT_TRUE(lockfree_spsc_queue_full(&q));
    TEST_ASSERT_EQUAL_UINT32(3u, lockfree_spsc_queue_count(&q));

    QueuePair overflow = {.a = 99, .b = 100};
    TEST_ASSERT_FALSE(lockfree_spsc_queue_push(&q, &overflow));
}

TEST(test_lockfree_spsc_queue_wraparound_preserves_order) {
    QueuePair storage[4];
    LockFreeSpscQueue q;
    TEST_ASSERT_TRUE(lockfree_spsc_queue_init(&q, storage, 4u, sizeof(storage[0])));

    for (int round = 0; round < 4; round++) {
        for (int i = 0; i < 4; i++) {
            QueuePair in = {.a = round * 100 + i, .b = round};
            TEST_ASSERT_TRUE(lockfree_spsc_queue_push(&q, &in));
        }
        for (int i = 0; i < 4; i++) {
            QueuePair out = {0};
            TEST_ASSERT_TRUE(lockfree_spsc_queue_pop(&q, &out));
            TEST_ASSERT_EQUAL_INT32(round * 100 + i, out.a);
            TEST_ASSERT_EQUAL_INT32(round, out.b);
        }
        TEST_ASSERT_TRUE(lockfree_spsc_queue_empty(&q));
    }
}

TEST(test_lockfree_spsc_queue_non_pow2_wraparound_preserves_order) {
    QueuePair storage[3];
    LockFreeSpscQueue q;
    TEST_ASSERT_TRUE(lockfree_spsc_queue_init(&q, storage, 3u, sizeof(storage[0])));

    for (int round = 0; round < 4; round++) {
        for (int i = 0; i < 3; i++) {
            QueuePair in = {.a = round * 10 + i, .b = 1000 + round};
            TEST_ASSERT_TRUE(lockfree_spsc_queue_push(&q, &in));
        }
        for (int i = 0; i < 3; i++) {
            QueuePair out = {0};
            TEST_ASSERT_TRUE(lockfree_spsc_queue_pop(&q, &out));
            TEST_ASSERT_EQUAL_INT32(round * 10 + i, out.a);
            TEST_ASSERT_EQUAL_INT32(1000 + round, out.b);
        }
        TEST_ASSERT_TRUE(lockfree_spsc_queue_empty(&q));
    }
}

TEST(test_lockfree_spsc_queue_reset_clears_state) {
    QueuePair storage[2];
    LockFreeSpscQueue q;
    TEST_ASSERT_TRUE(lockfree_spsc_queue_init(&q, storage, 2u, sizeof(storage[0])));

    QueuePair in0 = {.a = 1, .b = 2};
    QueuePair in1 = {.a = 3, .b = 4};
    TEST_ASSERT_TRUE(lockfree_spsc_queue_push(&q, &in0));
    TEST_ASSERT_TRUE(lockfree_spsc_queue_push(&q, &in1));
    TEST_ASSERT_TRUE(lockfree_spsc_queue_full(&q));

    lockfree_spsc_queue_reset(&q);
    TEST_ASSERT_TRUE(lockfree_spsc_queue_empty(&q));
    TEST_ASSERT_FALSE(lockfree_spsc_queue_full(&q));
    TEST_ASSERT_EQUAL_UINT32(0u, lockfree_spsc_queue_count(&q));

    QueuePair out = {0};
    TEST_ASSERT_FALSE(lockfree_spsc_queue_pop(&q, &out));
}

TEST(test_lockfree_spsc_queue_init_rejects_invalid_config) {
    QueuePair storage[1];
    LockFreeSpscQueue q = {0};

    TEST_ASSERT_FALSE(lockfree_spsc_queue_init(NULL, storage, 1u, sizeof(storage[0])));
    TEST_ASSERT_FALSE(lockfree_spsc_queue_init(&q, NULL, 1u, sizeof(storage[0])));
    TEST_ASSERT_FALSE(lockfree_spsc_queue_init(&q, storage, 0u, sizeof(storage[0])));
    TEST_ASSERT_FALSE(lockfree_spsc_queue_init(&q, storage, 1u, 0u));
    TEST_ASSERT_FALSE(lockfree_spsc_queue_init(&q, storage, 0x80000000u, sizeof(storage[0])));
}
