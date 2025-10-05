/*
 * Test helper macros for improved readability in assertions.
 */
#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include "../unity.h"
#include "../CljObject.h"

// Assert that an object is not null and has the expected type
#define ASSERT_TYPE(OBJ, TYPE_KIND) \
    do { \
        TEST_ASSERT_NOT_NULL((OBJ)); \
        TEST_ASSERT_EQUAL_INT((TYPE_KIND), (OBJ)->type); \
    } while (0)

// Assert integer value inside a CljObject
#define ASSERT_OBJ_INT_EQ(OBJ, EXPECTED) \
    do { \
        ASSERT_TYPE((OBJ), CLJ_INT); \
        TEST_ASSERT_EQUAL_INT((EXPECTED), (OBJ)->as.i); \
    } while (0)

// Assert boolean value inside a CljObject (EXPECTED treated as bool)
#define ASSERT_OBJ_BOOL_EQ(OBJ, EXPECTED) \
    do { \
        ASSERT_TYPE((OBJ), CLJ_BOOL); \
        TEST_ASSERT_EQUAL_INT(((EXPECTED) != 0), (OBJ)->as.b); \
    } while (0)

// Assert C-string value inside a CljObject
#define ASSERT_OBJ_CSTR_EQ(OBJ, EXPECTED) \
    do { \
        ASSERT_TYPE((OBJ), CLJ_STRING); \
        TEST_ASSERT_NOT_NULL((OBJ)->as.data); \
        TEST_ASSERT_EQUAL_STRING((EXPECTED), (char*)(OBJ)->as.data); \
    } while (0)

// Assert float value inside a CljObject within tolerance EPS
#define ASSERT_OBJ_FLOAT_NEAR(OBJ, EXPECTED, EPS) \
    do { \
        ASSERT_TYPE((OBJ), CLJ_FLOAT); \
        TEST_ASSERT_FLOAT_WITHIN((EPS), (EXPECTED), (OBJ)->as.f); \
    } while (0)

#endif // TEST_HELPERS_H


