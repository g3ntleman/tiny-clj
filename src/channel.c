#include "channel.h"
#include "map.h"
#include "value.h"
#include "symbol.h"

CljMap* make_result_channel(void) {
    // Use capacity 4 to ensure we have room for updates (even if key lookup fails)
    CljMap *m = make_map(4);
    // map_assoc always returns a new map (COW disabled)
    CljObject *kw_value = intern_symbol(NULL, ":value");
    CljObject *kw_closed = intern_symbol(NULL, ":closed");
    // map_assoc returns a new map, so we need to use the return value
    CljMap *new_m = map_assoc(m, (ID)kw_value, NULL);
    RELEASE(m);  // Release old map
    m = map_assoc(new_m, (ID)kw_closed, (ID)clj_false);
    RELEASE(new_m);  // Release intermediate map
    return m;
}

ID result_channel_put(ID chan, ID value) {
    if (!chan) return NULL;
    CljObject *kw_value = intern_symbol(NULL, ":value");
    // map_assoc always returns a new map (COW disabled)
    return map_assoc(chan, (ID)kw_value, value);
}

ID result_channel_close(ID chan) {
    if (!chan) return NULL;
    CljObject *kw_closed = intern_symbol(NULL, ":closed");
    // map_assoc always returns a new map (COW disabled)
    return map_assoc(chan, (ID)kw_closed, (ID)clj_true);
}


