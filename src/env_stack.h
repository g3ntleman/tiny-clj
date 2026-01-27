#ifndef TINY_CLJ_ENV_STACK_H
#define TINY_CLJ_ENV_STACK_H

// Env-stack abstraction backed by COW persistent vectors.
//
// Important for performance:
// - Avoid accidental RETAIN on the stack itself; that would raise rc>1 and force
//   Copy-on-Write paths in vector operations.
// - Avoid persistent(transient(pv)) patterns; transient conversion copies.

#include "map.h"
#include "vector.h"

// NULL means empty stack.
static inline unsigned int env_stack_count(CljPersistentVector *stack) {
    return vector_count(stack);
}

static inline CljMap *env_stack_top(CljPersistentVector *stack) {
    unsigned int cnt = vector_count(stack);
    if (cnt == 0) return NULL;
    return (CljMap*)vector_nth(stack, cnt - 1);
}

// depth=0 returns top, depth=1 returns one below top, etc.
static inline CljMap *env_stack_get_from_top(CljPersistentVector *stack, unsigned int depth) {
    unsigned int cnt = vector_count(stack);
    if (cnt == 0 || depth >= cnt) return NULL;
    return (CljMap*)vector_nth(stack, (cnt - 1) - depth);
}

// Push/pop update the slot in-place without retaining the stack.
static inline void env_stack_push_inplace(CljPersistentVector **stack_slot, CljMap *env) {
    if (!stack_slot) return;
    if (!*stack_slot) {
        // Start with a small capacity to avoid immediate growth copies.
        *stack_slot = make_vector(4, CLJ_VECTOR_PERSISTENT);
    }
    vector_conj_inplace(stack_slot, env);
}

static inline void env_stack_pop_inplace(CljPersistentVector **stack_slot) {
    if (!stack_slot || !*stack_slot) return;
    if (vector_count(*stack_slot) == 0) return;
    vector_pop_inplace(stack_slot);
}

// Iterate from top to bottom (reverse order).
#define ENV_STACK_FOR_EACH_REVERSE(stack, elem_var) \
    for (int _i = (int)vector_count((stack)) - 1; (stack) && _i >= 0; --_i) \
        for (ID *_data_ptr = vector_as_array((stack)); _data_ptr; _data_ptr = NULL) \
            for (ID elem_var = _data_ptr[_i]; _data_ptr; _data_ptr = NULL)

#endif // TINY_CLJ_ENV_STACK_H

