#include "fx_timeline_ingress.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "event_loop.h"
#include "eval.h"
#include "map.h"
#include "memory.h"
#include "namespace.h"
#include "scene.h"
#include "symbol.h"

#define FX_TIMELINE_EDGE_CACHE_CAP 64u

typedef struct {
    bool used;
    uint8_t slot_index;
    ID entity_id;
    ID event_id;
    bool last_at_end;
} VgTimelineEdgeCacheEntry;

typedef struct {
    ID event_id;
    ID entity_id;
    uint16_t step_index;
    uint16_t keyframe_count;
    uint32_t phase_ms;
    uint32_t period_ms;
    bool end_event;
    bool at_end;
} VgTimelineIngressCtx;

static VgTimelineEdgeCacheEntry g_timeline_edge_cache[FX_TIMELINE_EDGE_CACHE_CAP];
static bool g_timeline_current_slot_valid = false;
static uint8_t g_timeline_current_slot = 0u;

static ID g_kw_timeline_event_id = NULL;
static ID g_kw_timeline_end_event = NULL;
static ID g_kw_timeline_at_end = NULL;
static ID g_kw_timeline_entity_id = NULL;
static ID g_kw_timeline_phase_ms = NULL;
static ID g_kw_timeline_period_ms = NULL;
static ID g_kw_timeline_step_index = NULL;
static ID g_kw_timeline_keyframe_count = NULL;
static ID g_sym_tiny_clj_event_ns = NULL;
static ID g_sym_dispatch_timeline_progress = NULL;

static void fx_timeline_keyword_cache_init(void) {
    if (!g_kw_timeline_event_id) g_kw_timeline_event_id = intern_symbol_global(":event-id");
    if (!g_kw_timeline_end_event) g_kw_timeline_end_event = intern_symbol_global(":end-event");
    if (!g_kw_timeline_at_end) g_kw_timeline_at_end = intern_symbol_global(":at-end");
    if (!g_kw_timeline_entity_id) g_kw_timeline_entity_id = intern_symbol_global(":entity-id");
    if (!g_kw_timeline_phase_ms) g_kw_timeline_phase_ms = intern_symbol_global(":phase-ms");
    if (!g_kw_timeline_period_ms) g_kw_timeline_period_ms = intern_symbol_global(":period-ms");
    if (!g_kw_timeline_step_index) g_kw_timeline_step_index = intern_symbol_global(":step-index");
    if (!g_kw_timeline_keyframe_count) g_kw_timeline_keyframe_count = intern_symbol_global(":keyframe-count");
    if (!g_sym_tiny_clj_event_ns) g_sym_tiny_clj_event_ns = intern_symbol_global("tiny-clj.event");
    if (!g_sym_dispatch_timeline_progress &&
        g_sym_tiny_clj_event_ns &&
        TAG(g_sym_tiny_clj_event_ns) == CLJ_SYMBOL) {
        g_sym_dispatch_timeline_progress =
            intern_symbol(as_symbol(g_sym_tiny_clj_event_ns), "dispatch-timeline-progress!");
    }
}

static ID fx_timeline_dispatch_fn(EvalState *st) {
    if (!st) {
        return NULL;
    }
    fx_timeline_keyword_cache_init();
    if (!g_sym_dispatch_timeline_progress || TAG(g_sym_dispatch_timeline_progress) != CLJ_SYMBOL) {
        return NULL;
    }
    ID dispatch_fn = ns_resolve(st, as_symbol(g_sym_dispatch_timeline_progress));
    if (!dispatch_fn || dispatch_fn == NOT_FOUND) {
        return NULL;
    }
    unsigned char fn_tag = TAG(dispatch_fn);
    if (fn_tag != CLJ_FUNC && fn_tag != CLJ_CLOSURE) {
        return NULL;
    }
    return dispatch_fn;
}

static void fx_timeline_ingress_cleanup(void *ctx) {
    VgTimelineIngressCtx *ingress_ctx = (VgTimelineIngressCtx *)ctx;
    if (!ingress_ctx) {
        return;
    }
    RELEASE(ingress_ctx->event_id);
    RELEASE(ingress_ctx->entity_id);
    CLJ_FREE(ingress_ctx);
}

static void fx_timeline_ingress_run(void *ctx, EvalState *st) {
    VgTimelineIngressCtx *ingress_ctx = (VgTimelineIngressCtx *)ctx;
    if (!ingress_ctx) {
        return;
    }
    ID dispatch_fn = fx_timeline_dispatch_fn(st);
    if (!dispatch_fn) {
        return;
    }
    CljPersistentMap *progress = make_map_from_kv(8u,
                                                  g_kw_timeline_event_id, ingress_ctx->event_id,
                                                  g_kw_timeline_end_event, ingress_ctx->end_event ? clj_true : clj_false,
                                                  g_kw_timeline_at_end, ingress_ctx->at_end ? clj_true : clj_false,
                                                  g_kw_timeline_entity_id, ingress_ctx->entity_id,
                                                  g_kw_timeline_phase_ms, fixnum((int32_t)ingress_ctx->phase_ms),
                                                  g_kw_timeline_period_ms, fixnum((int32_t)ingress_ctx->period_ms),
                                                  g_kw_timeline_step_index, fixnum((int32_t)ingress_ctx->step_index),
                                                  g_kw_timeline_keyframe_count, fixnum((int32_t)ingress_ctx->keyframe_count));
    if (!progress) {
        return;
    }
    ID args[1] = {progress};
    (void)eval_function_call(dispatch_fn, args, 1u, NULL, st);
    RELEASE(progress);
}

static void fx_timeline_edge_cache_reset(void) {
    memset(g_timeline_edge_cache, 0, sizeof(g_timeline_edge_cache));
}

static bool fx_timeline_edge_cache_should_emit(uint8_t slot_index, ID entity_id, ID event_id, bool at_end) {
    int first_free = -1;
    for (uint32_t i = 0; i < FX_TIMELINE_EDGE_CACHE_CAP; i++) {
        VgTimelineEdgeCacheEntry *entry = &g_timeline_edge_cache[i];
        if (!entry->used) {
            if (first_free < 0) {
                first_free = (int)i;
            }
            continue;
        }
        if (entry->slot_index == slot_index &&
            entry->entity_id == entity_id &&
            entry->event_id == event_id) {
            if (entry->last_at_end == at_end) {
                return false;
            }
            entry->last_at_end = at_end;
            return true;
        }
    }
    if (first_free >= 0) {
        VgTimelineEdgeCacheEntry *entry = &g_timeline_edge_cache[first_free];
        entry->used = true;
        entry->slot_index = slot_index;
        entry->entity_id = entity_id;
        entry->event_id = event_id;
        entry->last_at_end = at_end;
    }
    return true;
}

static void fx_timeline_progress_observer(ID entity_id,
                                          const VgTimelineProgressSample *sample) {
    if (!g_timeline_current_slot_valid || !entity_id || !sample ||
        !sample->end_event || !sample->event_id ||
        TAG(sample->event_id) != CLJ_SYMBOL) {
        return;
    }
    if (!fx_timeline_edge_cache_should_emit(g_timeline_current_slot,
                                            entity_id,
                                            sample->event_id,
                                            sample->at_end)) {
        return;
    }
    VgTimelineIngressCtx *ingress_ctx =
        (VgTimelineIngressCtx *)CLJ_MALLOC(sizeof(VgTimelineIngressCtx));
    if (!ingress_ctx) {
        return;
    }
    ingress_ctx->event_id = RETAIN(sample->event_id);
    ingress_ctx->entity_id = RETAIN(entity_id);
    ingress_ctx->step_index = sample->step_index;
    ingress_ctx->keyframe_count = sample->keyframe_count;
    ingress_ctx->phase_ms = sample->phase_ms;
    ingress_ctx->period_ms = sample->period_ms;
    ingress_ctx->end_event = sample->end_event;
    ingress_ctx->at_end = sample->at_end;
    if (!event_loop_enqueue_ingress_native(fx_timeline_ingress_run,
                                           ingress_ctx,
                                           fx_timeline_ingress_cleanup)) {
        fx_timeline_ingress_cleanup(ingress_ctx);
    }
}

void fx_timeline_ingress_init(void) {
    fx_timeline_edge_cache_reset();
    g_timeline_current_slot_valid = false;
    vg_set_timeline_progress_observer(fx_timeline_progress_observer);
}

void fx_timeline_ingress_shutdown(void) {
    vg_set_timeline_progress_observer(NULL);
    g_timeline_current_slot_valid = false;
    fx_timeline_edge_cache_reset();
}

void fx_timeline_ingress_set_current_slot(uint8_t slot_index) {
    g_timeline_current_slot = slot_index;
    g_timeline_current_slot_valid = true;
}

void fx_timeline_ingress_clear_current_slot(void) {
    g_timeline_current_slot_valid = false;
}
