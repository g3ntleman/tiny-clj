#include <stdbool.h>
#include "runtime.h"
#include "seq.h"
#include "strings.h"

// Provide a zero-initialized runtime instance so Subjective-C can link
TinyClJRuntime g_runtime = {0};

// Provide stub implementations for the seq iterator helpers used by to_string()
bool seq_iter_empty(const SeqIterator *iter) {
    (void)iter;
    return true;
}

ID seq_iter_first(const SeqIterator *iter) {
    (void)iter;
    return NULL;
}

bool seq_iter_next(SeqIterator *iter) {
    (void)iter;
    return false;
}
