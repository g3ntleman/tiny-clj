#ifndef TINY_CLJ_BUILTINS_TINY_FX_GFX_H
#define TINY_CLJ_BUILTINS_TINY_FX_GFX_H

#include "builtins.h"

#ifdef DEBUG
ID native_tinyfx_gfx_bench_vector_scene_bench(ID *args, unsigned int argc);
#endif
ID native_tinyclj_runtime_start_renderer(ID *args, unsigned int argc);
ID native_tinyclj_runtime_stop_renderer(ID *args, unsigned int argc);
ID native_tinyclj_runtime_renderer_state(ID *args, unsigned int argc);
ID native_tinyclj_runtime_renderer_timeline_step(ID *args, unsigned int argc);
ID native_tinyclj_runtime_renderer_timeline_progress(ID *args, unsigned int argc);
void builtins_tiny_fx_gfx_reset_cached_state(void);

#endif
