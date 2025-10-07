#include "common.h"
#include "platform.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

void platform_init() {
}

void platform_print(const char *message) {
    if (message == NULL) {
        return;
    }
    printf("%s\n", message);
}

const char *platform_name() {
    return "macOS";
}

int platform_set_stdin_nonblocking(int enable) {
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags == -1) return -1;
    if (enable) flags |= O_NONBLOCK; else flags &= ~O_NONBLOCK;
    return fcntl(STDIN_FILENO, F_SETFL, flags);
}

int platform_readline_nb(char *buf, int max) {
    if (!buf || max <= 1) return -1;
    static char linebuf[2048];
    static int len = 0;
    char tmp[256];
    ssize_t n = read(STDIN_FILENO, tmp, sizeof(tmp));
    if (n <= 0) {
        // no data or error (EAGAIN handled by caller as 0)
        return 0;
    }
    if (len + (int)n >= (int)sizeof(linebuf)) {
        len = 0; // overflow protection: reset buffer
        return -1;
    }
    memcpy(linebuf + len, tmp, (size_t)n);
    len += (int)n;
    // Check for newline
    for (int i = 0; i < len; i++) {
        if (linebuf[i] == '\n') {
            int outlen = (i < max - 1) ? i : (max - 1);
            memcpy(buf, linebuf, (size_t)outlen);
            buf[outlen] = '\0';
            // shift remaining
            int remaining = len - (i + 1);
            memmove(linebuf, linebuf + i + 1, (size_t)remaining);
            len = remaining;
            return outlen;
        }
    }
    return 0; // no full line yet
}
