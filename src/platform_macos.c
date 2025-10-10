#include "platform.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <stdbool.h>

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
    
    // Handle very large buffers gracefully
    if (max >= 10000) {
        // For very large buffers, just return 0 (no data available)
        // This prevents potential issues with huge buffer allocations
        return 0;
    }
    
    static char linebuf[2048];
    static int len = 0;
    char tmp[256];
    ssize_t n = read(STDIN_FILENO, tmp, sizeof(tmp));
    if (n == 0) {
        // EOF detected - flush terminal input buffer
        tcflush(STDIN_FILENO, TCIFLUSH);
        if (len > 0) {
            // Return any buffered content first
            int outlen = (len < max - 1) ? len : (max - 1);
            memcpy(buf, linebuf, (size_t)outlen);
            buf[outlen] = '\0';
            len = 0;
            return outlen;
        }
        return -1;
    }
    if (n < 0) {
        if (errno == EAGAIN) {
            // No data available (non-blocking)
            return 0;
        }
        // Real error
        return -1;
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

// Line editor platform functions
int platform_get_char(void) {
    char c;
    // Use blocking read for line editor
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0) return -1;
    return (unsigned char)c;
}

void platform_put_char(char c) {
    putchar(c);
    fflush(stdout);
}

void platform_put_string(const char *s) {
    printf("%s", s);
    fflush(stdout);
}

// Raw mode support for line editor
static struct termios original_termios;
static bool raw_mode_enabled = false;

void platform_set_raw_mode(int enable) {
    if (enable && !raw_mode_enabled) {
        // Save original terminal settings
        tcgetattr(STDIN_FILENO, &original_termios);
        
        struct termios raw = original_termios;
        // Disable canonical mode and echo
        raw.c_lflag &= ~(ICANON | ECHO);
        // Set minimum characters to read
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        raw_mode_enabled = true;
    } else if (!enable && raw_mode_enabled) {
        // Restore original terminal settings
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
        raw_mode_enabled = false;
    }
}
