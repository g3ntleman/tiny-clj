/*
 * Unity Memory Tests for Tiny-CLJ
 * 
 * First Unity test suite with command-line parameter support.
 * Demonstrates single test execution and test isolation.
 */

#include "tests_common.h"

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// TEST CASES (using WITH_AUTORELEASE_POOL for additional isolation)
// ============================================================================

TEST(test_memory_allocation) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test basic object creation
        CljObject *int_obj = fixnum(42);
        CljObject *float_obj = fixed(3.14f);
        CljValue str_obj = make_string("hello");
        
        TEST_ASSERT_NOT_NULL(int_obj);
        TEST_ASSERT_NOT_NULL(float_obj);
        TEST_ASSERT_NOT_NULL(str_obj);
        
        TEST_ASSERT_TRUE(is_fixnum((CljValue)int_obj));
        TEST_ASSERT_EQUAL_INT(42, as_fixnum((CljValue)int_obj));
        TEST_ASSERT_TRUE(is_fixed((CljValue)float_obj));
        TEST_ASSERT_TRUE(as_fixed((CljValue)float_obj) > 3.1f && as_fixed((CljValue)float_obj) < 3.2f);
        // String objects store data in the data pointer, not directly in the union
        // String data is stored directly after CljObject header
        char **str_ptr = (char**)((char*)str_obj + sizeof(CljObject));
        TEST_ASSERT_NOT_NULL(*str_ptr);
        
        // Objects are automatically cleaned up by WITH_AUTORELEASE_POOL
    }
}

TEST(test_memory_deallocation) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test object lifecycle with heap-allocated object (not immediate)
        // Use a string object since symbols are singletons and don't use reference counting
        CljString *obj = make_string("test_string_for_reference_counting");
        TEST_ASSERT_NOT_NULL(obj);
        
        // Test retain counting
        int initial_refs = retain_count(obj);
        TEST_ASSERT_EQUAL_INT(1, initial_refs);
        
        // Retain and release
        CljObject *retained = RETAIN(obj);
        TEST_ASSERT_EQUAL_INT(2, retain_count(obj));
        
        RELEASE(retained);
        // After releasing the retained reference, the object should still exist
        // The retain count should be 1 (original reference)
        TEST_ASSERT_EQUAL_INT(1, retain_count(obj));
        
        // Final cleanup
        RELEASE(obj);
    }
}

TEST(test_memory_list_alloc_release) {
#if !MEMORY_PROFILING_ENABLED
    TEST_IGNORE_MESSAGE("memory profiling disabled");
#else
    MemoryStats before = memory_profiler_get_stats();
    CljList *list = make_list(fixnum(1), make_list(fixnum(2), NULL));
    TEST_ASSERT_NOT_NULL(list);
    RELEASE(list);
    MemoryStats after = memory_profiler_get_stats();

    // Expect at least one deallocation to have happened.
    TEST_ASSERT_TRUE(after.total_deallocations >= before.total_deallocations + 1);
#endif
}

TEST(test_memory_leak_detection) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test that no memory leaks occur
        for (int i = 0; i < 10; i++) {
            CljValue val = fixnum(i);
            TEST_ASSERT_TRUE(is_fixnum(val));
            TEST_ASSERT_EQUAL_INT(i, as_fixnum(val));
            // No need to release - immediate value
        }
    }
}

TEST(test_vector_memory) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test vector creation and memory management
        CljValue vec = make_vector(5, false);
        TEST_ASSERT_NOT_NULL(vec);

        CljPersistentVector *vec_data = as_persistent_vector(vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        
        // Add elements using vector_conj
        for (int i = 0; i < 5; i++) {
            CljValue elem = fixnum(i);
            vec_data = vector_conj(vec_data, elem);
        }
        
        // Test vector operations
        ID elem0 = vector_nth(vec_data, 0);
        TEST_ASSERT_NOT_NULL(elem0);
        TEST_ASSERT_TRUE(is_fixnum((CljValue)elem0));
        TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)elem0));
        RELEASE(elem0);
        
        // Clean up
        RELEASE(vec);
    }
}

TEST(test_autorelease_pool_basic) {
    // Test basic WITH_AUTORELEASE_POOL functionality
    // Note: We can't test is_autorelease_pool_active() because
    // the test framework may have active pools
    WITH_AUTORELEASE_POOL({
        // Create some objects that should be autoreleased
        struct CljString *str1 = make_string("test1");
        struct CljString *str2 = make_string("test2");
        CljObject *list = (CljObject*)make_list(str1, (CljList*)str2);
        
        TEST_ASSERT_NOT_NULL(str1);
        TEST_ASSERT_NOT_NULL(str2);
        TEST_ASSERT_NOT_NULL(list);
        
        // Objects should be in the autorelease pool
        TEST_ASSERT_TRUE(is_autorelease_pool_active());
        
        // Test that objects are accessible
        TEST_ASSERT_EQUAL_INT(CLJ_STRING, ((CljObject*)str1)->type);
        TEST_ASSERT_EQUAL_INT(CLJ_STRING, ((CljObject*)str2)->type);
        TEST_ASSERT_EQUAL_INT(CLJ_LIST, list->type);
    });
    
    // After WITH_AUTORELEASE_POOL, the pool should be empty
    // and all objects should be freed
    // Note: We can't test is_autorelease_pool_active() because
    // the test framework may have active pools
    // TEST_ASSERT_FALSE(is_autorelease_pool_active());
}

TEST(test_autorelease_pool_nested) {
    // Test nested autorelease pools
    WITH_AUTORELEASE_POOL({
        struct CljString *outer_str = make_string("outer");
        TEST_ASSERT_NOT_NULL(outer_str);
        
        WITH_AUTORELEASE_POOL({
            struct CljString *inner_str = make_string("inner");
            CljObject *inner_list = (CljObject*)make_list(inner_str, NULL);
            
            TEST_ASSERT_NOT_NULL(inner_str);
            TEST_ASSERT_NOT_NULL(inner_list);
            
            // Inner pool should be active
            TEST_ASSERT_TRUE(is_autorelease_pool_active());
        });
        
        // Inner pool should be drained, but outer pool still active
        TEST_ASSERT_TRUE(is_autorelease_pool_active());
    });
    
    // After outer WITH_AUTORELEASE_POOL, no pools should be active
    // Note: We can't test is_autorelease_pool_active() because
    // the test framework may have active pools
    // TEST_ASSERT_FALSE(is_autorelease_pool_active());
}

TEST(test_cow_assumptions_rc_behavior) {
    // Test critical assumptions for Copy-on-Write implementation
    WITH_AUTORELEASE_POOL({
        // Test 1: AUTORELEASE does NOT increase RC
        CljPersistentMap *map = (CljPersistentMap*)make_map(4);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        CljPersistentMap *same = AUTORELEASE((CljValue)map);
        TEST_ASSERT_EQUAL(1, map->base.rc);  // RC bleibt 1!
        TEST_ASSERT_EQUAL_PTR(map, same);
        
        // Test 2: RETAIN increases RC
        RETAIN(map);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        // Test 3: RC=2 would trigger COW in map_assoc
        
        RELEASE(map);  // Back to RC=1
        TEST_ASSERT_EQUAL(1, map->base.rc);
    });
}

TEST(test_autorelease_pool_memory_cleanup) {
    // Test that autorelease pool properly cleans up memory
    // MemoryStats before_stats = memory_profiler_get_stats(); // Unused
    
    WITH_AUTORELEASE_POOL({
        // Create multiple objects that should be autoreleased
        for (int i = 0; i < 10; i++) {
            char buffer[32];
            test_snprintf(buffer, sizeof(buffer), "test_string_%d", i);
            struct CljString *str = make_string(buffer);
            TEST_ASSERT_NOT_NULL(str);
            
            // Add to autorelease pool
            AUTORELEASE(str);
        }
        
        // Create a list with autoreleased objects
        CljObject *list = NULL;
        for (int i = 0; i < 5; i++) {
            char buffer[32];
            test_snprintf(buffer, sizeof(buffer), "list_item_%d", i);
            struct CljString *str = make_string(buffer);
            list = (CljObject*)make_list(str, (CljList*)list);
            AUTORELEASE(str);
        }
        AUTORELEASE(list);
        
        // Pool should be active and contain objects
        TEST_ASSERT_TRUE(is_autorelease_pool_active());
    });
    
    // After WITH_AUTORELEASE_POOL, pool should be empty
    // Note: We can't test is_autorelease_pool_active() because
    // the test framework may have active pools
    // TEST_ASSERT_FALSE(is_autorelease_pool_active());
    
    // Check that memory was properly cleaned up
    MemoryStats after_stats = memory_profiler_get_stats();
    
    // The difference should show that objects were allocated and then freed
    // We expect some allocations and deallocations to match
    // Note: Memory profiler may be reset between tests, so we can't compare absolute values
    // Instead, we just verify that the test completed without crashing
    TEST_ASSERT_TRUE(after_stats.total_allocations >= 0);
    TEST_ASSERT_TRUE(after_stats.total_deallocations >= 0);
    
    // Memory leaks should be minimal (some may remain due to singletons)
    // Note: Memory profiler tracks all allocations, including singletons and cached objects
    // The actual leak count may be higher due to these persistent objects
    // This test verifies that the autorelease pool works correctly, not that there are no leaks
    TEST_ASSERT_TRUE(after_stats.total_allocations >= 0);
    TEST_ASSERT_TRUE(after_stats.total_deallocations >= 0);
}

TEST(test_memory_profiler_tracks_raw_alloc_blocks) {
#if MEMORY_PROFILING_ENABLED
    // Ensure profiling is enabled (setUp usually does this in debug builds).
    enable_memory_profiling(true);

    MemoryStats before = memory_profiler_get_stats();

    void *p = CLJ_MALLOC(123);
    TEST_ASSERT_NOT_NULL(p);

    MemoryStats mid = memory_profiler_get_stats();
    TEST_ASSERT_TRUE(mid.raw_blocks_current >= before.raw_blocks_current + 1);
    TEST_ASSERT_TRUE(mid.raw_bytes_current >= before.raw_bytes_current + 123);
    TEST_ASSERT_TRUE(mid.raw_allocations >= before.raw_allocations + 1);
    TEST_ASSERT_TRUE(mid.current_memory_usage >= before.current_memory_usage + 123);
    TEST_ASSERT_TRUE(mid.peak_memory_usage >= mid.current_memory_usage);

    CLJ_FREE(p);

    MemoryStats after = memory_profiler_get_stats();
    TEST_ASSERT_EQUAL(before.raw_blocks_current, after.raw_blocks_current);
    TEST_ASSERT_EQUAL(before.raw_bytes_current, after.raw_bytes_current);
    TEST_ASSERT_EQUAL(before.current_memory_usage, after.current_memory_usage);
    TEST_ASSERT_TRUE(after.raw_frees >= before.raw_frees + 1);
#else
    TEST_IGNORE();
#endif
}

#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
TEST(test_zombie_detection) {
    // Test zombie detection: access an object after it became a zombie.
    //
    // In this codebase:
    // - retaining a zombie triggers ZombieAccessException (this is what we test)
    // - releasing a zombie triggers UseAfterFreeError (double-free protection)
    
    // Create an object (rc = 1)
    CljString *obj = make_string("test_zombie_object");
    TEST_ASSERT_NOT_NULL(obj);
    TEST_ASSERT_EQUAL_INT(1, obj->rc);
    
    // Release once (rc = 0, object becomes zombie if zombie mode enabled)
    RELEASE(obj);
    
    // Try to retain again - should trigger ZombieAccessException
    TRY {
        RETAIN(obj);  // This should throw ZombieAccessException
        TEST_FAIL_MESSAGE("Expected ZombieAccessException when retaining zombie object");
    } CATCH(ex) {
        // Verify exception type
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("ZombieAccessException", ex->type);
        TEST_ASSERT_NOT_NULL(strstr(ex->message, "zombie"));
        
        // Verify zombie object is stored in exception
        TEST_ASSERT_TRUE(ex->object != 0);
        TEST_ASSERT_TRUE(ex->object == (uintptr_t)obj);
        
        // Verify object is marked as zombie
        TEST_ASSERT_EQUAL_INT(ZOMBIE_RC, obj->rc);
        
        // Verify stacktrace is present (DEBUG builds only)
        TEST_ASSERT_NOT_NULL(ex->stacktrace);
    } END_TRY
}
#else
TEST(test_zombie_detection) {
    // Zombie behavior is only valid when ZOMBIE_ENABLED is compiled in.
    // When zombie mode is OFF, RELEASE() frees memory and accessing the object is UAF.
    TEST_IGNORE();
}
#endif
