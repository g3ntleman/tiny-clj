#ifndef TINY_CLJ_FX_TIMELINE_INGRESS_H
#define TINY_CLJ_FX_TIMELINE_INGRESS_H

#include <stdint.h>

void fx_timeline_ingress_init(void);
void fx_timeline_ingress_shutdown(void);
void fx_timeline_ingress_set_current_slot(uint8_t slot_index);
void fx_timeline_ingress_clear_current_slot(void);

#endif
