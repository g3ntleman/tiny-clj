#ifndef TINY_CLJ_ENVIRONMENT_H
#define TINY_CLJ_ENVIRONMENT_H

#include "object.h"
#include "value.h"

// Environment helpers for function calls
/** Create child env extended with param/value bindings (stack impl.). */
CljObject* env_extend_stack(CljObject *parent_env, CljObject **params, CljObject **values, int count);

#endif
