#ifndef TINY_CLJ_ATOM_H
#define TINY_CLJ_ATOM_H

#include <subjective-c/object.h>
#include "common.h"  // For CLJ_ASSERT

// CljAtom struct definition
typedef struct {
    CljObject base;
    ID value;  // The current value (can be any type, including NULL/nil)
} CljAtom;

// Type-safe casting
static inline CljAtom* as_atom(ID obj) {
    // Happy path: obj is not NULL and has correct type
    if (obj && TAG(obj) == CLJ_ATOM) {
        return (CljAtom*)obj;  // Direct return, no jumps
    }
    CLJ_ASSERT(0 && "Expected Atom type");
    return NULL;  // Never reached in DEBUG, but needed for Release builds
}

// === Atom API ===
/** Create an atom with initial value.
 * @param value Initial value (can be NULL/nil or immediate)
 * @return New atom object with RC=1 (caller must release)
 */
CljAtom* make_atom(ID value);

/** Get the current value of an atom.
 * @param atom Atom object
 * @return Current value (caller must release if not immediate)
 */
ID atom_deref(CljAtom *atom);

/** Set the value of an atom directly.
 * @param atom Atom object
 * @param new_value New value (can be NULL/nil or immediate)
 * @return New value (caller must release if not immediate)
 */
ID atom_reset(CljAtom *atom, ID new_value);

/** Apply a function to the atom's value and update it.
 * @param atom Atom object
 * @param fn Function to apply
 * @param args Additional arguments (can be NULL)
 * @param argc Number of additional arguments
 * @return New value (caller must release if not immediate)
 */
ID atom_swap(CljAtom *atom, ID fn, ID *args, unsigned int argc);

#endif // TINY_CLJ_ATOM_H

