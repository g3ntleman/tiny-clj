#ifndef TINY_CLJ_PANEL_BACKEND_H
#define TINY_CLJ_PANEL_BACKEND_H

#include "panel.h"
#include "vector_scene_graph.h"

bool vg_panel_backend_submit_clip_rect(VgPanel *panel,
                                       const VgFrameBuffer *fb,
                                       VgClipRect rect);

#endif
