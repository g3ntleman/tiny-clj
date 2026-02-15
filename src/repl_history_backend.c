#include "repl_history_backend.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "memory.h"
#include "repl_history_common.h"
#include "to_string.h"
#include "strings.h"

#define REPL_HISTORY_DEFAULT_MAX_READ_BYTES (64u * 1024u)
#define REPL_HISTORY_DEFAULT_TRIM_NUM 8
#define REPL_HISTORY_DEFAULT_TRIM_DEN 10

/**
 * @brief Resolves effective byte limit for serialized history payload.
 *
 * @param backend Backend descriptor.
 * @return Effective byte limit, or SIZE_MAX when unlimited.
 */
static size_t repl_history_effective_byte_limit(const ReplHistoryBackend *backend)
{
    if (!backend) {
        return SIZE_MAX;
    }

    size_t limit = backend->default_byte_limit;
    if (limit == 0) {
        limit = SIZE_MAX;
    }

    if (backend->effective_limit && limit != SIZE_MAX) {
        size_t adjusted = backend->effective_limit(backend->ctx, limit);
        if (adjusted > 0) {
            limit = adjusted;
        }
    }
    return limit;
}

/**
 * @brief Trims history by serialized byte limit using geometric shrink steps.
 *
 * @param vec Source history vector.
 * @param byte_limit Maximum allowed serialized size in bytes.
 * @param trim_num Shrink numerator.
 * @param trim_den Shrink denominator.
 * @return Retained trimmed vector that fits @p byte_limit.
 */
static CljPersistentVector *repl_history_trim_to_byte_limit(CljPersistentVector *vec,
                                                            size_t byte_limit,
                                                            int trim_num,
                                                            int trim_den)
{
    if (!vec || byte_limit == 0) {
        return (CljPersistentVector*)RETAIN(empty_vector());
    }

    if (byte_limit == SIZE_MAX) {
        return (CljPersistentVector*)RETAIN(vec);
    }

    int keep_count = vector_count(vec);
    if (keep_count <= 0) {
        return (CljPersistentVector*)RETAIN(empty_vector());
    }

    int n = (trim_num > 0) ? trim_num : REPL_HISTORY_DEFAULT_TRIM_NUM;
    int d = (trim_den > 0) ? trim_den : REPL_HISTORY_DEFAULT_TRIM_DEN;
    if (d <= 0) {
        d = REPL_HISTORY_DEFAULT_TRIM_DEN;
    }

    while (keep_count > 0) {
        CljPersistentVector *candidate = repl_history_take_last(vec, keep_count);
        CljString *repr = candidate ? pr_str(candidate) : NULL;
        size_t repr_len = repr ? string_length(repr) : (size_t)-1;
        if (repr && repr_len <= byte_limit) {
            return candidate;
        }
        RELEASE(candidate);

        if (keep_count == 1) {
            break;
        }
        int reduced = (keep_count * n) / d;
        if (reduced >= keep_count) {
            reduced = keep_count - 1;
        }
        if (reduced < 1) {
            reduced = 1;
        }
        keep_count = reduced;
    }

    return (CljPersistentVector*)RETAIN(empty_vector());
}

/**
 * @brief Verifies persisted payload by reading it back and comparing bytes.
 *
 * @param backend Backend descriptor.
 * @param expected Expected serialized bytes.
 * @param expected_len Number of expected bytes.
 * @return true when persisted payload matches exactly.
 */
static bool repl_history_verify_payload(const ReplHistoryBackend *backend,
                                        const uint8_t *expected,
                                        size_t expected_len)
{
    if (!backend || !backend->query_size || !backend->read) {
        return false;
    }

    size_t stored_len = 0;
    if (!backend->query_size(backend->ctx, &stored_len) || stored_len != expected_len) {
        return false;
    }

    uint8_t *actual = (uint8_t*)CLJ_MALLOC(stored_len == 0 ? 1u : stored_len);
    if (!actual) {
        return false;
    }

    size_t loaded_len = 0;
    bool ok = backend->read(backend->ctx, actual, stored_len, &loaded_len) &&
              loaded_len == expected_len &&
              memcmp(actual, expected, expected_len) == 0;
    CLJ_FREE(actual);
    return ok;
}

/**
 * @brief Tries to serialize history as EDN vector of strings without generic pr_str().
 *
 * This fast-path keeps the on-disk format identical (e.g. ["cmd1" "cmd2"]) but
 * avoids traversing the generic printer for the common REPL history case.
 *
 * @param history History vector candidate.
 * @param out_payload Receives heap buffer with serialized bytes (caller frees).
 * @param out_len Receives byte length (without trailing NUL).
 * @return true when fast-path serialization succeeds.
 */
static bool repl_history_serialize_string_vector(CljPersistentVector *history,
                                                 uint8_t **out_payload,
                                                 size_t *out_len)
{
    if (!history || !out_payload || !out_len) {
        return false;
    }
    *out_payload = NULL;
    *out_len = 0;

    int count = vector_count(history);
    size_t total_len = 2; // '[' and ']'

    for (int i = 0; i < count; i++) {
        ID elem = vector_nth(history, (unsigned int)i);
        if (!elem || TAG(elem) != CLJ_STRING) {
            return false;
        }
        total_len += escape_string_calc_length((CljString*)elem);
        if (i < count - 1) {
            total_len += 1; // space
        }
    }

    uint8_t *buf = (uint8_t*)CLJ_MALLOC(total_len + 1u);
    if (!buf) {
        return false;
    }

    size_t off = 0;
    buf[off++] = '[';
    for (int i = 0; i < count; i++) {
        ID elem = vector_nth(history, (unsigned int)i);
        escape_string_write((CljString*)elem, (char*)buf, &off);
        if (i < count - 1) {
            buf[off++] = ' ';
        }
    }
    buf[off++] = ']';
    buf[off] = '\0';

    if (off != total_len) {
        CLJ_FREE(buf);
        return false;
    }

    *out_payload = buf;
    *out_len = total_len;
    return true;
}

/**
 * @brief Loads REPL history from a backend and parses it as EDN vector.
 *
 * @param backend Persistence backend descriptor.
 * @param st Evaluation state used for EDN parsing.
 * @return Retained persistent vector (empty vector on load/parse failure).
 */
CljPersistentVector *repl_history_backend_load(const ReplHistoryBackend *backend, EvalState *st)
{
    if (!backend || !st || !backend->query_size || !backend->read) {
        return (CljPersistentVector*)RETAIN(empty_vector());
    }

    size_t saved_len = 0;
    if (!backend->query_size(backend->ctx, &saved_len) || saved_len == 0) {
        return (CljPersistentVector*)RETAIN(empty_vector());
    }

    size_t max_read = (backend->max_read_bytes > 0) ? backend->max_read_bytes : REPL_HISTORY_DEFAULT_MAX_READ_BYTES;
    if (saved_len > max_read) {
        return (CljPersistentVector*)RETAIN(empty_vector());
    }

    char *buf = (char*)CLJ_MALLOC(saved_len + 1u);
    if (!buf) {
        return (CljPersistentVector*)RETAIN(empty_vector());
    }

    size_t loaded_len = 0;
    if (!backend->read(backend->ctx, (uint8_t*)buf, saved_len, &loaded_len) || loaded_len == 0) {
        CLJ_FREE(buf);
        return (CljPersistentVector*)RETAIN(empty_vector());
    }

    if (loaded_len > saved_len) {
        loaded_len = saved_len;
    }
    buf[loaded_len] = '\0';
    CljPersistentVector *vec = repl_history_parse_vector(buf, backend->source_name, st);
    CLJ_FREE(buf);
    return vec;
}

/**
 * @brief Persists a history vector through a backend.
 *
 * Applies max-entry trimming and byte-limit trimming before serialization.
 *
 * @param backend Persistence backend descriptor.
 * @param history History vector to persist.
 * @return true when persistence (and optional verification) succeeds.
 */
bool repl_history_backend_save(const ReplHistoryBackend *backend, CljPersistentVector *history)
{
    if (!backend || !history || !backend->write) {
        return false;
    }

    CljPersistentVector *work = (CljPersistentVector*)RETAIN(history);
    if (backend->max_entries > 0) {
        CljPersistentVector *trimmed_entries = repl_history_take_last(work, (int)backend->max_entries);
        RELEASE(work);
        work = trimmed_entries;
    }

    size_t byte_limit = repl_history_effective_byte_limit(backend);
    CljPersistentVector *trimmed_bytes = repl_history_trim_to_byte_limit(
        work, byte_limit, backend->trim_num, backend->trim_den);
    RELEASE(work);
    work = trimmed_bytes;

    uint8_t *owned_payload = NULL;
    size_t len = 0;
    const uint8_t *payload = NULL;

    bool fast_path_ok = repl_history_serialize_string_vector(work, &owned_payload, &len);
    if (fast_path_ok) {
        payload = owned_payload;
    }

    CljString *repr = NULL;
    if (!payload) {
        repr = pr_str(work);
        if (!repr) {
            RELEASE(work);
            return false;
        }
        len = string_length(repr);
        payload = (const uint8_t*)string_data(repr);
    }

    RELEASE(work);
    if (byte_limit != SIZE_MAX && len > byte_limit) {
        CLJ_FREE(owned_payload);
        return false;
    }

    if (!backend->write(backend->ctx, payload, len)) {
        CLJ_FREE(owned_payload);
        return false;
    }
    if (backend->sync && !backend->sync(backend->ctx)) {
        CLJ_FREE(owned_payload);
        return false;
    }

    if (backend->verify_after_save) {
        if (!repl_history_verify_payload(backend, payload, len)) {
            CLJ_FREE(owned_payload);
            return false;
        }
    }

    if (backend->verify_after_reopen) {
        if (!backend->reopen_for_verify || !backend->reopen_for_verify(backend->ctx)) {
            CLJ_FREE(owned_payload);
            return false;
        }
        if (!repl_history_verify_payload(backend, payload, len)) {
            CLJ_FREE(owned_payload);
            return false;
        }
    }

    CLJ_FREE(owned_payload);
    return true;
}

/**
 * @brief Persists current line editor history through a backend.
 *
 * @param backend Persistence backend descriptor.
 * @param editor Line editor whose history is persisted.
 * @return true when persistence succeeds.
 */
bool repl_history_backend_save_from_editor(const ReplHistoryBackend *backend, LineEditor *editor)
{
    if (!editor) {
        return false;
    }

    CljPersistentVector *history = line_editor_get_history_vector(editor);
    if (!history) {
        return false;
    }

    bool saved = repl_history_backend_save(backend, history);
    RELEASE(history);
    return saved;
}
