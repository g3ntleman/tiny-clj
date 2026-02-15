#ifndef REPL_HISTORY_COMMON_H
#define REPL_HISTORY_COMMON_H

#include "eval.h"
#include "memory.h"
#include "object.h"
#include "parser.h"
#include "reader.h"
#include "vector.h"

/**
 * @brief Copies the most recent history entries into a new vector.
 *
 * @param vec Source history vector.
 * @param keep_count Number of trailing entries to keep.
 * @return Retained vector containing at most @p keep_count trailing entries.
 */
static inline CljPersistentVector *repl_history_take_last(CljPersistentVector *vec, int keep_count)
{
    if (!vec || keep_count <= 0) {
        return (CljPersistentVector*)RETAIN(empty_vector());
    }

    int count = vector_count(vec);
    if (count <= keep_count) {
        return (CljPersistentVector*)RETAIN(vec);
    }

    int start = count - keep_count;
    CljPersistentVector *out = make_vector(keep_count, STRONG);
    if (!out) {
        return (CljPersistentVector*)RETAIN(empty_vector());
    }

    for (int i = start; i < count; i++) {
        vector_conj_inplace(&out, vector_nth(vec, i));
    }
    return out;
}

/**
 * @brief Parses serialized EDN history into a persistent vector.
 *
 * @param edn Serialized EDN history text.
 * @param source_name Source label for reader diagnostics.
 * @param st Active evaluator state.
 * @return Retained parsed vector or an empty vector on parse/type failure.
 */
static inline CljPersistentVector *repl_history_parse_vector(const char *edn,
                                                             const char *source_name,
                                                             EvalState *st)
{
    if (!edn || !st) {
        return (CljPersistentVector*)RETAIN(empty_vector());
    }

    CljPersistentVector *parsed_vec = NULL;
    WITH_AUTORELEASE_POOL({
        TRY {
            Reader rd;
            reader_init(&rd, edn);
            reader_set_source_name(&rd, source_name ? source_name : "<repl history>");
            ID parsed = value_by_parsing_expr(&rd, st);
            if (parsed && TAG(parsed) == CLJ_VECTOR_PERSISTENT) {
                parsed_vec = (CljPersistentVector*)RETAIN(parsed);
            }
        } CATCH(ex) {
            (void)ex;
            parsed_vec = NULL;
        } END_TRY
    });

    return parsed_vec ? parsed_vec : (CljPersistentVector*)RETAIN(empty_vector());
}

#endif /* REPL_HISTORY_COMMON_H */
