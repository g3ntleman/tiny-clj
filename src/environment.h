#ifndef TINY_CLJ_ENVIRONMENT_H
#define TINY_CLJ_ENVIRONMENT_H

#include "object.h"
#include "value.h"
#include "map.h"

// Environment helpers for function calls
/** Create child env extended with param/value bindings (stack impl.). */
CljMap* env_extend_stack(CljMap *parent_env, ID *params, ID *values, int count);

#endif
