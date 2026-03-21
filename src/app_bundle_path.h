#ifndef TINY_CLJ_APP_BUNDLE_PATH_H
#define TINY_CLJ_APP_BUNDLE_PATH_H

#include <stdbool.h>

/**
 * @brief Return true when @p path points at a macOS app-bundle executable.
 *
 * Expected shape: `.../<name>.app/Contents/MacOS/<name>`.
 */
bool tinyclj_path_is_app_bundle_executable(const char *path);

/**
 * @brief Return true when the current process is running from a macOS app bundle.
 */
bool tinyclj_process_is_app_bundle(void);

#endif /* TINY_CLJ_APP_BUNDLE_PATH_H */
