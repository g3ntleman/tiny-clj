#ifndef TINY_CLJ_ENVIRONMENT_H
#define TINY_CLJ_ENVIRONMENT_H

#include "object.h"
#include "value.h"
#include "map.h"
#include "list.h"
#include "symbol.h"

// Environment helpers for function calls
/** Create new environment stack with param/value bindings (idiomatic CljList of maps). */
CljList* env_extend_stack(CljList *parent_stack, ID *params, ID *values, int count);

#endif
