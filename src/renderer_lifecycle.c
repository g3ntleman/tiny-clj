#include "renderer_lifecycle.h"

typedef struct {
    TinyRendererStartCallback start_cb;
    TinyRendererStopCallback stop_cb;
    void *user_data;
    bool running;
} TinyRendererLifecycleState;

static TinyRendererLifecycleState g_renderer_lifecycle = {0};

void tiny_renderer_lifecycle_set_callbacks(TinyRendererStartCallback start_cb,
                                           TinyRendererStopCallback stop_cb,
                                           void *user_data) {
    g_renderer_lifecycle.start_cb = start_cb;
    g_renderer_lifecycle.stop_cb = stop_cb;
    g_renderer_lifecycle.user_data = user_data;
    g_renderer_lifecycle.running = false;
}

bool tiny_renderer_lifecycle_start(ID slot_atoms) {
    if (!g_renderer_lifecycle.start_cb) {
        return false;
    }
    if (g_renderer_lifecycle.running) {
        return true;
    }
    if (!g_renderer_lifecycle.start_cb(slot_atoms, g_renderer_lifecycle.user_data)) {
        return false;
    }
    g_renderer_lifecycle.running = true;
    return true;
}

bool tiny_renderer_lifecycle_stop(void) {
    if (!g_renderer_lifecycle.start_cb || !g_renderer_lifecycle.stop_cb) {
        return false;
    }
    if (!g_renderer_lifecycle.running) {
        return true;
    }
    if (!g_renderer_lifecycle.stop_cb(g_renderer_lifecycle.user_data)) {
        return false;
    }
    g_renderer_lifecycle.running = false;
    return true;
}

bool tiny_renderer_lifecycle_is_available(void) {
    return g_renderer_lifecycle.start_cb && g_renderer_lifecycle.stop_cb;
}

bool tiny_renderer_lifecycle_is_running(void) {
    return g_renderer_lifecycle.running;
}
