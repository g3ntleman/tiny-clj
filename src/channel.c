#include "channel.h"
#include "map.h"
#include "value.h"
#include "symbol.h"

CljMap* make_result_channel(void) {
    // Use capacity 4 to ensure we have room for updates (even if key lookup fails)
    CljMap *m = make_map(4);
    // CRITICAL: Ensure map has RC=1 for in-place mutation
    // make_map returns RC=1, so map_assoc will mutate in-place
    CljObject *kw_value = intern_symbol(NULL, ":value");
    CljObject *kw_closed = intern_symbol(NULL, ":closed");
    // These will mutate in-place since m has RC=1
    (void)map_assoc((ID)m, (ID)kw_value, NULL);
    (void)map_assoc((ID)m, (ID)kw_closed, (ID)clj_false);
    return m;
}

void result_channel_put(ID chan, ID value) {
    if (!chan) return;
    CljObject *kw_value = intern_symbol(NULL, ":value");
    // Channel should always be RC=1 (freshly created), so this will mutate in-place
    (void)map_assoc((ID)chan, (ID)kw_value, value);
}

void result_channel_close(CljObject *chan) {
    if (!chan) return;
    CljObject *kw_closed = intern_symbol(NULL, ":closed");
    // Channel should always be RC=1 (freshly created), so this will mutate in-place
    (void)map_assoc((ID)chan, (ID)kw_closed, (ID)clj_true);
}


