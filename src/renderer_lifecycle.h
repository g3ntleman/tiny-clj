#ifndef TINY_CLJ_RENDERER_LIFECYCLE_H
#define TINY_CLJ_RENDERER_LIFECYCLE_H

#include <stdbool.h>
#include "value.h"

typedef bool (*TinyRendererStartCallback)(ID slot_atoms, void *user_data);
typedef bool (*TinyRendererStopCallback)(void *user_data);

/* Install or clear renderer lifecycle callbacks. Passing NULL clears support. */
void tiny_renderer_lifecycle_set_callbacks(TinyRendererStartCallback start_cb,
                                           TinyRendererStopCallback stop_cb,
                                           void *user_data);

/* Start renderer via registered callback. Idempotent: true when already running. */
bool tiny_renderer_lifecycle_start(ID slot_atoms);

/* Stop renderer via registered callback. Idempotent: true when already stopped. */
bool tiny_renderer_lifecycle_stop(void);

/* Renderer lifecycle callback pair is installed. */
bool tiny_renderer_lifecycle_is_available(void);

/* Renderer has been started via tiny_renderer_lifecycle_start and not stopped yet. */
bool tiny_renderer_lifecycle_is_running(void);

#endif /* TINY_CLJ_RENDERER_LIFECYCLE_H */
