#include "eval_sequence.h"
#include "eval.h"
#include "builtins.h"
#include "exception.h"
#include "event_loop.h"
#include "error_messages.h"
#include "map.h"
#include "seq.h"
#include "strings.h"

// eval_and_call_native_with_context is defined as static inline in eval_sequence.h

ID eval_map_lookup(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx, ID map) {
    int total_count = list_count(list);
    int argc = total_count - 1;

    if (argc != 1) {
        return throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
            "Wrong number of args (%d) passed to: clojure.lang.PersistentArrayMap", argc);
    }

    ID key = eval_arg_with_context(list, 1, env, st, ctx);
    if (!key) return NULL;

    ID result = map_get((CljValue)map, (CljValue)key, NULL);
    RELEASE(key);
    return result;
}
