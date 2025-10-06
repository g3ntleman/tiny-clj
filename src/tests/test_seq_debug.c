#include <stdio.h>
#include <stdlib.h>
#include "minunit.h"
#include "seq.h"
#include "vector.h"
#include "CljObject.h"
#include "memory_profiler.h"

// Test-First: Debug seq_create with vector
static char* test_seq_create_vector_debug(void) {
    printf("=== DEBUGGING seq_create with Vector ===\n");
    
    // Create a simple vector with 3 elements
    CljObject *string_objects[3];
    for (int i = 0; i < 3; i++) {
        char str[10];
        sprintf(str, "Item%d", i);
        string_objects[i] = make_string(str);
    }
    CljObject *test_vector = vector_from_items(string_objects, 3);
    
    printf("Created vector with %d elements\n", as_vector(test_vector)->count);
    
    // Try to create seq
    SeqIterator *seq_iter = seq_create(test_vector);
    printf("seq_create returned: %p\n", (void*)seq_iter);
    
    if (seq_iter) {
        printf("Seq type: %d, State: %p\n", seq_iter->seq_type, (void*)seq_iter->state);
        printf("Container: %p\n", (void*)seq_iter->container);
        
        // Try seq_empty
        bool empty = seq_empty(seq_iter);
        printf("seq_empty: %s\n", empty ? "true" : "false");
        
        // Try seq_count
        int count = seq_count(seq_iter);
        printf("seq_count: %d\n", count);
        
        // Try seq_first
        CljObject *first = seq_first(seq_iter);
        printf("seq_first returned: %p\n", (void*)first);
        if (first) {
            printf("First element type: %d\n", first->type);
        }
        
        free(seq_iter);
    }
    
    // Cleanup
    for (int i = 0; i < 3; i++) {
        release(string_objects[i]);
    }
    release(test_vector);
    
    return 0;
}

// Test-First: Debug seq iteration (like in comparison test)
static char* test_seq_iteration_debug(void) {
    printf("=== DEBUGGING seq iteration ===\n");
    
    // Create a simple vector with 3 elements
    CljObject *string_objects[3];
    for (int i = 0; i < 3; i++) {
        char str[10];
        sprintf(str, "Item%d", i);
        string_objects[i] = make_string(str);
    }
    CljObject *test_vector = vector_from_items(string_objects, 3);
    
    printf("Created vector with %d elements\n", as_vector(test_vector)->count);
    
    // Create seq from vector (like in comparison test)
    SeqIterator *seq_iter = seq_create(test_vector);
    printf("seq_create returned: %p\n", (void*)seq_iter);
    mu_assert("Seq should be created", seq_iter != NULL);
    
    // Iterate through seq (EXACTLY like in comparison test)
    int count = 0;
    SeqIterator *current = seq_iter;
    while (current && !seq_empty(current)) {
        printf("Iteration %d: current=%p, empty=%s\n", count, (void*)current, seq_empty(current) ? "true" : "false");
        
        CljObject *first_elem = seq_first(current);
        printf("  seq_first returned: %p\n", (void*)first_elem);
        if (first_elem) {
            printf("  Element type: %d\n", first_elem->type);
            count++;
        }
        
        SeqIterator *next = seq_rest(current);
        printf("  seq_rest returned: %p\n", (void*)next);
        
        if (current != seq_iter) {
            free(current); // Free intermediate iterators
        }
        current = next;
    }
    
    printf("Final count: %d\n", count);
    mu_assert("Should iterate over 3 elements", count == 3);
    
    // Cleanup
    if (seq_iter) free(seq_iter);
    for (int i = 0; i < 3; i++) {
        release(string_objects[i]);
    }
    release(test_vector);
    
    return 0;
}

static char* all_tests(void) {
    mu_run_test(test_seq_create_vector_debug);
    mu_run_test(test_seq_iteration_debug);
    return 0;
}

void setUp(void) {
    // Empty setup
}

void tearDown(void) {
    // Empty teardown
}

int main(void) {
    printf("=== SEQ DEBUG TESTS ===\n\n");
    
    char *result = all_tests();
    
    if (result != 0) {
        printf("FAILED: %s\n", result);
    } else {
        printf("ALL TESTS PASSED\n");
    }
    
    return result != 0;
}
