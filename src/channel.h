#ifndef TINY_CLJ_CHANNEL_H
#define TINY_CLJ_CHANNEL_H

#include "object.h"

// Result channel API (promise-chan like)
CljObject* make_result_channel(void);           // returns a map {:value nil :closed false}
void result_channel_put(CljObject *chan, CljObject *value);
void result_channel_close(CljObject *chan);

#endif

