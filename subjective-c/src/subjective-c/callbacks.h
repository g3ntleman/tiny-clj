#ifndef SUBJECTIVE_C_CALLBACKS_H
#define SUBJECTIVE_C_CALLBACKS_H

#include "object.h"
#include "strings.h"
#include <stdint.h>
#include <stdbool.h>

// Callback-Typen
typedef uint32_t (*CljHashFn)(ID value);
typedef bool (*CljEqualFn)(ID a, ID b);
typedef CljString* (*CljToStringFn)(ID value);

// Callback-Struct für alle tiny-clj Funktionen
typedef struct {
    CljHashFn hash;
    CljEqualFn equal;
    CljToStringFn to_string;
} CljCallbacks;

// Globaler Callback-Struct
extern CljCallbacks g_clj_callbacks;

/** @brief Compute hash of value
 * @param value Value to hash
 * @return Hash code
 */
uint32_t clj_hash(ID value);

/** @brief Check equality of two values
 * @param a First value
 * @param b Second value
 * @return True if equal
 */
bool clj_equal(ID a, ID b);

/** @brief Convert value to string
 * @param value Value to convert
 * @return String representation
 */
CljString* clj_to_string(ID value);

/** @brief Default hash implementation
 * @param value Value to hash
 * @return Hash code
 */
uint32_t clj_hash_default(ID value);

/** @brief Default equality implementation
 * @param a First value
 * @param b Second value
 * @return True if equal
 */
bool clj_equal_default(ID a, ID b);

/** @brief Default string conversion
 * @param value Value to convert
 * @return String representation
 */
CljString* clj_to_string_default(ID value);

/** @brief Set all callbacks at once
 * @param callbacks Callback struct
 */
void clj_set_callbacks(CljCallbacks callbacks);

/** @brief Set hash function callback
 * @param fn Hash function
 */
void clj_set_hash_fn(CljHashFn fn);

/** @brief Set equality function callback
 * @param fn Equality function
 */
void clj_set_equal_fn(CljEqualFn fn);

/** @brief Set string conversion callback
 * @param fn String conversion function
 */
void clj_set_to_string_fn(CljToStringFn fn);

#endif // SUBJECTIVE_C_CALLBACKS_H

