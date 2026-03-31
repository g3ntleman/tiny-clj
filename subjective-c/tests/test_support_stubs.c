#include <stdbool.h>
#include "atom.h"
#include "runtime.h"
#include "strings.h"

// Provide a zero-initialized runtime instance so Subjective-C can link
TinyClJRuntime g_runtime = {0};

/** subjective-c memory.c releases CLJ_ATOM via atom_destroy; tiny-clj atom.c is not linked here. */
void atom_destroy(CljAtom *atom) {
  if (!atom)
    return;
#if CLJ_ATOM_USE_MUTEX
  (void)pthread_mutex_destroy(&atom->mutex);
#endif
}
