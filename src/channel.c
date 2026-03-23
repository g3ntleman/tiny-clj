#include "channel.h"
#include "map.h"
#include "value.h"
#include "symbol.h"
#include "exception.h"  // For throw_oom

static ID g_channel_kw_value = NULL;
static ID g_channel_kw_closed = NULL;
static const IdSymbolCacheEntry g_channel_kw_cache[] = {
    {&g_channel_kw_value, ":value"},
    {&g_channel_kw_closed, ":closed"},
};

static inline bool channel_keywords_ready(void) {
    return id_symbol_cache_init_global(
        g_channel_kw_cache,
        sizeof(g_channel_kw_cache) / sizeof(g_channel_kw_cache[0]));
}

/** Create a channel (promise-like) as a transient map.
 * @return New transient map channel with RC=1 (caller must release)
 */
CljTransientMap* make_result_channel(void) {
    CljPersistentMap *base = make_map(4);
    if (!base) return NULL;
    CljTransientMap *tmap = map_transient(base);
    RELEASE(base);
    if (!tmap) return NULL;

    // Initialize with :value = nil and :closed = false
    if (!channel_keywords_ready()) {
        RELEASE(tmap);
        return NULL;
    }

    map_conj(tmap, g_channel_kw_value, NULL);  // :value = nil
    map_conj(tmap, g_channel_kw_closed, clj_false);  // :closed = false

    return tmap;
}

/** Put a value into the channel (mutates in-place using map_conj).
 * @param chan Channel (transient map)
 * @param value Value to put (can be NULL/nil or immediate)
 */
void result_channel_put(CljTransientMap *chan, ID value) {
    CLJ_ASSERT(chan != NULL);

    CLJ_ASSERT(channel_keywords_ready());
    map_conj(chan, g_channel_kw_value, value);
}

/** Close the channel (mutates in-place using map_conj).
 * @param chan Channel (transient map)
 */
void result_channel_close(CljTransientMap *chan) {
    CLJ_ASSERT(chan != NULL);

    CLJ_ASSERT(channel_keywords_ready());
    map_conj(chan, g_channel_kw_closed, clj_true);
}

