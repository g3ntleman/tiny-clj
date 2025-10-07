#ifndef TINY_CLJ_PLATFORM_H
#define TINY_CLJ_PLATFORM_H

void platform_init();
void platform_print(const char *message);
const char *platform_name();

// Non-blocking input support for cooperative multitasking
// Enable/disable non-blocking mode for stdin (returns 0 on success)
int platform_set_stdin_nonblocking(int enable);
// Read a line non-blocking: returns number of bytes read into buf (null-terminated if >0),
// 0 if no complete line available yet, -1 on error. Max includes space for terminator.
int platform_readline_nb(char *buf, int max);

#endif // TINY_CLJ_PLATFORM_H
