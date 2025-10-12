/*
 * MinUnit - Minimal Unit Testing Framework for C
 * 
 * Ultra-lightweight testing framework in just a few lines of code.
 * Perfect for embedded systems and simple projects.
 */

#ifndef MINUNIT_H
#define MINUNIT_H

#include <stdio.h>
#include <string.h>
#include "object.h"

// Core MinUnit macros with better error reporting
#define mu_assert(message, test) do { \
    if (!(test)) { \
        printf("FAILED: %s at %s:%d\n", message, __FILE__, __LINE__); \
        return message; \
    } \
} while(0)

#define mu_run_test(test) do { \
    char *message = test(); \
    tests_run++; \
    if (message) { \
        printf("❌ %s failed: %s\n", #test, message); \
        return message; \
    } \
} while(0)

#define mu_run_test_verbose(test) do { \
    printf("Running %s...\n", #test); \
    char *message = test(); \
    tests_run++; \
    if (message) { \
        printf("❌ %s failed: %s\n", #test, message); \
        return message; \
    } else { \
        printf("✅ %s passed\n", #test); \
    } \
} while(0)

int tests_run;

// CljObject-specific assertion helpers
#define mu_assert_obj_not_null(obj) \
    mu_assert("object is null", (obj) != NULL)

#define mu_assert_obj_type(obj, expected_type) \
    mu_assert("object is null", (obj) != NULL); \
    mu_assert("wrong object type", (obj)->type == (expected_type))

// Enhanced assertions with detailed error messages
#define mu_assert_obj_type_detailed(obj, expected_type) do { \
    if ((obj) == NULL) { \
        printf("FAILED: object is null at %s:%d\n", __FILE__, __LINE__); \
        return "object is null"; \
    } \
    if ((obj)->type != (expected_type)) { \
        printf("FAILED: wrong object type - got %d, expected %d at %s:%d\n", \
               (obj)->type, (expected_type), __FILE__, __LINE__); \
        return "wrong object type"; \
    } \
} while(0)

#define mu_assert_obj_int_detailed(obj, expected) do { \
    mu_assert_obj_type_detailed(obj, CLJ_INT); \
    if ((obj)->as.i != (expected)) { \
        printf("FAILED: wrong int value - got %d, expected %d at %s:%d\n", \
               (obj)->as.i, (expected), __FILE__, __LINE__); \
        return "wrong int value"; \
    } \
} while(0)

#define mu_assert_obj_int(obj, expected) \
    mu_assert("object is null", (obj) != NULL); \
    mu_assert("wrong object type", (obj)->type == CLJ_INT); \
    mu_assert("wrong int value", (obj)->as.i == (expected))

#define mu_assert_obj_bool(obj, expected) \
    mu_assert_obj_type(obj, CLJ_BOOL); \
    mu_assert("wrong bool value", (obj)->as.b == (expected))

#define mu_assert_obj_string(obj, expected) \
    mu_assert_obj_type(obj, CLJ_STRING); \
    mu_assert("string data is null", (obj)->as.data != NULL); \
    mu_assert("wrong string value", strcmp((char*)(obj)->as.data, (expected)) == 0)

#define mu_assert_obj_ptr_equal(obj1, obj2) \
    mu_assert("objects not equal", (obj1) == (obj2))

#define mu_assert_string_eq(actual, expected) \
    mu_assert("strings not equal", strcmp((actual), (expected)) == 0)

// Debug helper macros
#define mu_debug_obj(obj, name) do { \
    if (obj) { \
        printf("DEBUG %s: type=%d, rc=%d, ptr=%p\n", name, (obj)->type, (obj)->rc, obj); \
    } else { \
        printf("DEBUG %s: NULL\n", name); \
    } \
} while(0)

#define mu_debug_obj_int(obj, name) do { \
    if (type(obj) == CLJ_INT) { \
        printf("DEBUG %s: int value=%d\n", name, obj->as.i); \
    } else { \
        printf("DEBUG %s: not an int (type=%d)\n", name, obj ? obj->type : -1); \
    } \
} while(0)

// Test runner helper with detailed reporting
static inline int run_minunit_tests(char *(*all_tests)(void), const char *suite_name) {
    printf("\n🧪 === %s ===\n", suite_name);
    tests_run = 0;
    char *result = all_tests();
    if (result != 0) {
        printf("\n❌ SUITE FAILED: %s\n", result);
        printf("📊 Total tests run: %d\n", tests_run);
        return 1;
    } else {
        printf("\n✅ SUITE PASSED: All %d tests passed\n", tests_run);
        return 0;
    }
}

#endif // MINUNIT_H
