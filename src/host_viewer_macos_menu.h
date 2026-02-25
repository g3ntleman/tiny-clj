#ifndef MACOS_VIEWER_MENU_H
#define MACOS_VIEWER_MENU_H

#include <stdbool.h>

void macos_viewer_install_menu(void);
void macos_viewer_set_window_title(const char *title);
void macos_viewer_register_window_callbacks(void);
void macos_viewer_restore_window_position(void);
void macos_viewer_save_window_position(void);

#endif
