#ifndef TINY_CLJ_DEBUG_H
#define TINY_CLJ_DEBUG_H

#include "platform.h"

#ifdef ENABLE_DEBUG_STRINGS
  #define DEBUG_PRINT(msg) platform_print(msg)
#else
  #define DEBUG_PRINT(msg) ((void)0)
#endif

#endif /* TINY_CLJ_DEBUG_H */
