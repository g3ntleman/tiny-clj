#ifndef TINY_CLJ_CHANNEL_H
#define TINY_CLJ_CHANNEL_H

#include "object.h"
#include "map.h"

// Result channel API (promise-chan like)
CljMap* make_result_channel(void);           // returns a map {:value nil :closed false}
void result_channel_put(ID chan, ID value);
void result_channel_close(CljObject *chan);

#endif





