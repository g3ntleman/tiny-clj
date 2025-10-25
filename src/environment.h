#ifndef TINY_CLJ_ENVIRONMENT_H
#define TINY_CLJ_ENVIRONMENT_H

#include "object.h"
#include "value.h"

// Environment helpers for function calls
/** Create child env extended with param/value bindings (stack impl.). */
CljObject* env_extend_stack(CljObject *parent_env, CljObject **params, CljObject **values, int count);
/** Lookup key in env and return value or NULL. */
ID env_get_stack(CljObject *env, CljObject *key);
/** Set key->value in env (mutating env). */
void env_set_stack(CljObject *env, CljObject *key, CljObject *value);

#endif
