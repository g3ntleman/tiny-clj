#ifndef MACOS_FX_MENU_H
#define MACOS_FX_MENU_H

#include <stdbool.h>

void macos_fx_install_menu(void);
void macos_fx_set_window_title(const char *title);
void macos_fx_register_window_callbacks(void);
void macos_fx_restore_window_position(void);
void macos_fx_save_window_position(void);
void macos_fx_activate_app_window(void);
bool macos_fx_get_content_size(unsigned *out_w, unsigned *out_h);
void macos_fx_begin_performance_activity(void);
void macos_fx_end_performance_activity(void);
void macos_fx_start_runloop_watchdog(void);
void macos_fx_stop_runloop_watchdog(void);

#endif
