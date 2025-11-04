#include "channel.h"
#include "map.h"
#include "value.h"
#include "symbol.h"

CljObject* make_result_channel(void) {
    // Use capacity 4 to ensure we have room for updates (even if key lookup fails)
    CljMap *m = (CljMap*)make_map(4);
    CljObject *kw_value = intern_symbol(NULL, ":value");
    CljObject *kw_closed = intern_symbol(NULL, ":closed");
    (void)map_assoc_cow((CljValue)m, kw_value, NULL);
    (void)map_assoc_cow((CljValue)m, kw_closed, (CljValue)clj_false);
    return (CljObject*)m;
}

void result_channel_put(CljObject *chan, CljObject *value) {
    if (!chan) return;
    CljObject *kw_value = intern_symbol(NULL, ":value");
    (void)map_assoc_cow(chan, kw_value, value);
}

void result_channel_close(CljObject *chan) {
    if (!chan) return;
    CljObject *kw_closed = intern_symbol(NULL, ":closed");
    (void)map_assoc_cow(chan, kw_closed, (CljValue)clj_true);
}


