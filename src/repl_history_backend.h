#ifndef REPL_HISTORY_BACKEND_H
#define REPL_HISTORY_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "eval.h"
#include "line_editor.h"
#include "vector.h"

typedef struct ReplHistoryBackend ReplHistoryBackend;

/**
 * @brief Backend descriptor for persisted REPL history storage.
 *
 * All callbacks return true on success and false on failure.
 */
struct ReplHistoryBackend {
    void *ctx;
    const char *source_name;
    size_t max_read_bytes;
    size_t default_byte_limit;
    size_t max_entries;
    int trim_num;
    int trim_den;
    bool verify_after_save;
    bool verify_after_reopen;
    bool (*query_size)(void *ctx, size_t *out_size);
    bool (*read)(void *ctx, uint8_t *buf, size_t cap, size_t *out_size);
    bool (*write)(void *ctx, const uint8_t *buf, size_t len);
    bool (*sync)(void *ctx);
    size_t (*effective_limit)(void *ctx, size_t default_limit);
    bool (*reopen_for_verify)(void *ctx);
};

/**
 * @brief Loads REPL history from a backend and parses it as EDN vector.
 *
 * @param backend Persistence backend descriptor.
 * @param st Evaluation state used for EDN parsing.
 * @return Retained persistent vector (empty vector on load/parse failure).
 */
CljPersistentVector *repl_history_backend_load(const ReplHistoryBackend *backend, EvalState *st);

/**
 * @brief Persists a history vector through a backend.
 *
 * Applies max-entry trimming and byte-limit trimming before serialization.
 *
 * @param backend Persistence backend descriptor.
 * @param history History vector to persist.
 * @return true when persistence (and optional verification) succeeds.
 */
bool repl_history_backend_save(const ReplHistoryBackend *backend, CljPersistentVector *history);

#endif /* REPL_HISTORY_BACKEND_H */
