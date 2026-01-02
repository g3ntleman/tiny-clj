#ifndef TINY_CLJ_CHANNEL_H
#define TINY_CLJ_CHANNEL_H

#include <subjective-c/object.h>
#include <subjective-c/map.h>

// Channel API - Channels are implemented as transient maps (mutable)
// A channel is a transient map with :value and :closed keys

/** Create a channel (promise-like) as a transient map.
 * @return New transient map channel with RC=1 (caller must release)
 */
CljMap* make_result_channel(void);

/** Put a value into the channel (mutates in-place using map_conj).
 * @param chan Channel (transient map)
 * @param value Value to put (can be NULL/nil or immediate)
 */
void result_channel_put(CljMap *chan, ID value);

/** Close the channel (mutates in-place using map_conj).
 * @param chan Channel (transient map)
 */
void result_channel_close(CljMap *chan);

#endif





