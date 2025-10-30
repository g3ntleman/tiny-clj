#include "channel.h"
#include "map.h"
#include "value.h"
#include "symbol.h"

CljObject* make_result_channel(void) {
    CljMap *m = (CljMap*)make_map(2);
    CljObject *kw_value = intern_symbol(NULL, ":value");
    CljObject *kw_closed = intern_symbol(NULL, ":closed");
    map_assoc((CljObject*)m, kw_value, NULL);
    map_assoc((CljObject*)m, kw_closed, make_special(SPECIAL_FALSE));
    return (CljObject*)m;
}

void result_channel_put(CljObject *chan, CljObject *value) {
    if (!chan) return;
    CljObject *kw_value = intern_symbol(NULL, ":value");
    map_assoc(chan, kw_value, value);
}

void result_channel_close(CljObject *chan) {
    if (!chan) return;
    CljObject *kw_closed = intern_symbol(NULL, ":closed");
    map_assoc(chan, kw_closed, make_special(SPECIAL_TRUE));
}


