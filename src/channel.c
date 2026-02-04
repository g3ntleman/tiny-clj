#include "channel.h"
#include "map.h"
#include "value.h"
#include "symbol.h"
#include "exception.h"  // For throw_oom

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
    CljObject *kw_value = (CljObject*)intern_symbol_global(":value");
    CljObject *kw_closed = (CljObject*)intern_symbol_global(":closed");

    map_conj(tmap, kw_value, NULL);  // :value = nil
    map_conj(tmap, kw_closed, clj_false);  // :closed = false

    return tmap;
}

/** Put a value into the channel (mutates in-place using map_conj).
 * @param chan Channel (transient map)
 * @param value Value to put (can be NULL/nil or immediate)
 */
void result_channel_put(CljTransientMap *chan, ID value) {
    CLJ_ASSERT(chan != NULL);
    
    CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
    CLJ_ASSERT(kw_value != NULL);
    
    map_conj(chan, kw_value, value);
}

/** Close the channel (mutates in-place using map_conj).
 * @param chan Channel (transient map)
 */
void result_channel_close(CljTransientMap *chan) {
    CLJ_ASSERT(chan != NULL);
    
    CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
    CLJ_ASSERT(kw_closed != NULL);
    
    map_conj(chan, kw_closed, clj_true);
}

