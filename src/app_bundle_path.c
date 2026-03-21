#include "app_bundle_path.h"

#include <string.h>

#if defined(__APPLE__) && !defined(ESP32_BUILD)
#include <mach-o/dyld.h>
#include <limits.h>
#endif

bool tinyclj_path_is_app_bundle_executable(const char *path) {
    return path && strstr(path, ".app/Contents/MacOS/") != NULL;
}

bool tinyclj_process_is_app_bundle(void) {
#if defined(__APPLE__) && !defined(ESP32_BUILD)
    char executable_path[PATH_MAX];
    uint32_t executable_size = (uint32_t)sizeof(executable_path);
    if (_NSGetExecutablePath(executable_path, &executable_size) != 0) {
        return false;
    }
    return tinyclj_path_is_app_bundle_executable(executable_path);
#else
    return false;
#endif
}
