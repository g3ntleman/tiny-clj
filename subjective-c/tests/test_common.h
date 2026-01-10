#ifndef SUBJECTIVE_C_TEST_COMMON_H
#define SUBJECTIVE_C_TEST_COMMON_H

#include "unity.h"
#include "test_registry.h"
#include "subjective-c.h"
#include "memory.h"

// Convenience alias: Clojure-style "nil" in C tests.
// Unity uses "NULL", many tests talk about "nil".
#ifndef TEST_ASSERT_NIL
#define TEST_ASSERT_NIL TEST_ASSERT_NULL
#endif
#ifndef TEST_ASSERT_NIL_MESSAGE
#define TEST_ASSERT_NIL_MESSAGE TEST_ASSERT_NULL_MESSAGE
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#endif // SUBJECTIVE_C_TEST_COMMON_H
