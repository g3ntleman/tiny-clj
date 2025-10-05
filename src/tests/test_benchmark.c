/*
 * Simple Benchmark Test for Tiny-Clj Performance
 * 
 * Simplified version that actually works
 */

#include "../unity.h"
#include "../CljObject.h"
#include "../map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void setUp(void) {
    symbol_table_cleanup();
}

void tearDown(void) {
    // Cleanup
}

void test_primitive_object_creation_performance(void) {
    const int iterations = 100000;
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < iterations; i++) {
        CljObject *obj = autorelease(make_int(i));
        CljObject *obj2 = autorelease(make_string("test"));
        CljObject *obj3 = autorelease(clj_true());
        CljObject *obj4 = autorelease(clj_nil());
        (void)obj; (void)obj2; (void)obj3; (void)obj4;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double time_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
    double ops_per_sec = (iterations * 1000.0) / time_ms;
    
    printf("Primitive Object Creation: %.3f ms total, %.6f ms/iter, %.0f ops/sec\n", 
           time_ms, time_ms / iterations, ops_per_sec);
}

void test_type_checking_performance(void) {
    const int iterations = 1000000;
    CljObject *test_objects[4] = {
        autorelease(make_int(42)),
        autorelease(make_string("test")),
        autorelease(clj_true()),
        autorelease(make_vector(10, 1))
    };
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < iterations; i++) {
        CljObject *obj = test_objects[i % 4];
        int is_prim = IS_PRIMITIVE_TYPE(obj->type);
        (void)is_prim;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double time_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
    double ops_per_sec = (iterations * 1000.0) / time_ms;
    
    printf("Type Checking: %.3f ms total, %.6f ms/iter, %.0f ops/sec\n", 
           time_ms, time_ms / iterations, ops_per_sec);
}

void test_reference_counting_performance(void) {
    const int iterations = 100000;
    CljObject *obj = autorelease(make_string("test_string"));
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < iterations; i++) {
        retain(obj);
        release(obj);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double time_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
    double ops_per_sec = (iterations * 1000.0) / time_ms;
    
    printf("Reference Counting: %.3f ms total, %.6f ms/iter, %.0f ops/sec\n", 
           time_ms, time_ms / iterations, ops_per_sec);
}

void test_vector_creation_performance(void) {
    const int iterations = 10000;
    const int vector_size = 100;
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < iterations; i++) {
        CljObject *vec_obj = autorelease(make_vector(vector_size, 1));
        CljPersistentVector *vec = as_vector(vec_obj);
        int count = vec->count;
        int capacity = vec->capacity;
        (void)count; (void)capacity;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double time_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
    double ops_per_sec = (iterations * 1000.0) / time_ms;
    
    printf("Vector Creation: %.3f ms total, %.6f ms/iter, %.0f ops/sec\n", 
           time_ms, time_ms / iterations, ops_per_sec);
}

void test_map_operations_performance(void) {
    const int iterations = 1000;
    const int map_size = 50;
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < iterations; i++) {
        CljObject *map_obj = autorelease(make_map(map_size));
        
        // Fill map
        for (int j = 0; j < map_size; j++) {
            char key_str[32];
            snprintf(key_str, sizeof(key_str), "key_%d", j);
            CljObject *key = autorelease(make_string(key_str));
            CljObject *value = autorelease(make_int(j));
            map_assoc(map_obj, key, value);
        }
        
        // Access elements
        for (int j = 0; j < map_size; j++) {
            char key_str[32];
            snprintf(key_str, sizeof(key_str), "key_%d", j);
            CljObject *key = autorelease(make_string(key_str));
            CljObject *value = map_get(map_obj, key);
            (void)value;
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double time_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
    double ops_per_sec = (iterations * 1000.0) / time_ms;
    
    printf("Map Operations: %.3f ms total, %.6f ms/iter, %.0f ops/sec\n", 
           time_ms, time_ms / iterations, ops_per_sec);
}

void test_function_call_performance(void) {
    const int iterations = 10000;
    
    // Create a simple function
    CljObject *x_sym = autorelease(intern_symbol_global("x"));
    CljObject *y_sym = autorelease(intern_symbol_global("y"));
    CljObject *params[2] = {x_sym, y_sym};
    CljObject *body = autorelease(make_int(42));
    CljObject *func = autorelease(make_function(params, 2, body, NULL, "test_func"));
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < iterations; i++) {
        CljObject *arg1 = autorelease(make_int(i));
        CljObject *arg2 = autorelease(make_int(i * 2));
        CljObject *args[2] = {arg1, arg2};
        
        CljObject *result = autorelease(clj_call_function(func, 2, args));
        (void)result;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double time_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
    double ops_per_sec = (iterations * 1000.0) / time_ms;
    
    printf("Function Calls: %.3f ms total, %.6f ms/iter, %.0f ops/sec\n", 
           time_ms, time_ms / iterations, ops_per_sec);
}

int main(void) {
    printf("=== Tiny-Clj Performance Benchmarks ===\n\n");
    
    UNITY_BEGIN();
    
    RUN_TEST(test_primitive_object_creation_performance);
    RUN_TEST(test_type_checking_performance);
    RUN_TEST(test_reference_counting_performance);
    RUN_TEST(test_vector_creation_performance);
    RUN_TEST(test_map_operations_performance);
    RUN_TEST(test_function_call_performance);
    
    printf("\n=== Benchmark Summary ===\n");
    printf("All benchmarks completed successfully!\n");
    
    return UNITY_END();
}
