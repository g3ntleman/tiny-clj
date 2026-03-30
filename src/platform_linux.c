/**
 * platform_linux.c – Linux host platform implementation.
 *
 * Provides the same platform_* API surface as platform_macos.c so the
 * interpreter, REPL, and unit-tests can compile and run on Linux.
 *
 * Networking stubs return errors; the core interpreter and tests do not
 * require functional UDP/TCP/mDNS.
 */

#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/select.h>
#include <termios.h>

__attribute__((weak)) void tinyclj_stdout_observe_bytes(const char *data, size_t n) {
    (void)data;
    (void)n;
}

void platform_init(void) {
}

void platform_sleep_ms(unsigned int ms) {
    unsigned int usec = (ms > (UINT32_MAX / 1000u)) ? UINT32_MAX : (ms * 1000u);
    usleep(usec);
}

void platform_runloop_run_once(unsigned int timeout_ms) {
    (void)timeout_ms;
}

void platform_print(const char *message) {
    if (!message) return;
    fputs(message, stdout);
    fputc('\n', stdout);
    tinyclj_stdout_observe_bytes(message, strlen(message));
    tinyclj_stdout_observe_bytes("\n", 1);
}

uint32_t platform_current_time_ms(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) return 0;
    int32_t sec_in_day = (int32_t)(tv.tv_sec % 86400);
    if (sec_in_day < 0) sec_in_day = 0;
    int32_t millis = sec_in_day * 1000 + (int32_t)(tv.tv_usec / 1000);
    if (millis < 0) millis = 0;
    if (millis >= 86400000) millis = 86399999;
    return (uint32_t)millis;
}

const char *platform_name(void) { return "Linux"; }

const char *platform_os_version(void) { return NULL; }

size_t platform_heap_bytes_free(void) { return (size_t)-1; }
size_t platform_heap_bytes_total(void) { return (size_t)-1; }
size_t platform_ram_bytes_total(void) { return (size_t)-1; }
size_t platform_external_ram_bytes_total(void) { return (size_t)-1; }
size_t platform_flash_bytes_free(void) { return (size_t)-1; }
size_t platform_flash_bytes_total(void) { return (size_t)-1; }

void platform_hardware_info(PlatformHardwareInfo *out) {
    if (!out) return;
    out->valid = false;
    out->model[0] = '\0';
    out->cores = 0;
    out->revision = 0;
    out->gpio_pin_count = 0;
    out->features = 0;
}

int platform_set_stdin_nonblocking(int enable) {
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags == -1) return -1;
    if (enable) flags |= O_NONBLOCK; else flags &= ~O_NONBLOCK;
    return fcntl(STDIN_FILENO, F_SETFL, flags);
}

int platform_readline_nb(char *buf, int max) {
    if (!buf || max <= 1) return -1;
    int n = (int)read(STDIN_FILENO, buf, (size_t)(max - 1));
    if (n > 0) { buf[n] = '\0'; return n; }
    if (n == 0) return 0;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    return -1;
}

int platform_get_char(void *ctx) {
    (void)ctx;
    unsigned char c;
    int n = (int)read(STDIN_FILENO, &c, 1);
    return (n == 1) ? (int)c : -1;
}

void platform_put_char(void *ctx, char c) {
    (void)ctx;
    if (write(STDOUT_FILENO, &c, 1) < 0) { /* ignore */ }
    tinyclj_stdout_observe_bytes(&c, 1);
}

void platform_put_string(void *ctx, const char *s) {
    (void)ctx;
    if (!s) return;
    size_t len = strlen(s);
    if (write(STDOUT_FILENO, s, len) < 0) { /* ignore */ }
    tinyclj_stdout_observe_bytes(s, len);
}

static struct termios g_orig_termios;
static bool g_raw_mode_active;

void platform_set_raw_mode(int enable) {
    if (enable && !g_raw_mode_active) {
        tcgetattr(STDIN_FILENO, &g_orig_termios);
        struct termios raw = g_orig_termios;
        raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | ISIG | IEXTEN);
        raw.c_iflag &= (tcflag_t)~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
        raw.c_oflag &= (tcflag_t)~(OPOST);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        g_raw_mode_active = true;
    } else if (!enable && g_raw_mode_active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
        g_raw_mode_active = false;
    }
}

bool platform_try_get_cursor_position(uint16_t *row, uint16_t *col) {
    if (!row || !col) return false;
    if (write(STDOUT_FILENO, "\033[6n", 4) < 0) return false;
    fd_set fds;
    struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0) return false;
    char buf[32];
    int n = (int)read(STDIN_FILENO, buf, sizeof(buf) - 1);
    if (n < 6) return false;
    buf[n] = '\0';
    unsigned r = 0, c = 0;
    if (sscanf(buf, "\033[%u;%uR", &r, &c) == 2) {
        *row = (uint16_t)r;
        *col = (uint16_t)c;
        return true;
    }
    return false;
}

/* Networking stubs – return errors so callers know the feature is unavailable. */

PlatformUdpSocket* platform_udp_bind(uint16_t port, platform_udp_recv_cb cb, void *cb_ctx) {
    (void)port; (void)cb; (void)cb_ctx;
    return NULL;
}
int platform_udp_send(PlatformUdpSocket *sock, const uint8_t *data, size_t len, const char *to_addr, uint16_t to_port) {
    (void)sock; (void)data; (void)len; (void)to_addr; (void)to_port;
    return -1;
}
void platform_udp_close(PlatformUdpSocket *sock) { (void)sock; }

PlatformMdns* platform_mdns_open(platform_udp_recv_cb cb, void *cb_ctx) {
    (void)cb; (void)cb_ctx;
    return NULL;
}
int platform_mdns_send_unicast(PlatformMdns *m, const uint8_t *data, size_t len, const char *to_addr, uint16_t to_port) {
    (void)m; (void)data; (void)len; (void)to_addr; (void)to_port;
    return -1;
}
int platform_mdns_send_multicast(PlatformMdns *m, const uint8_t *data, size_t len) {
    (void)m; (void)data; (void)len;
    return -1;
}
void platform_mdns_close(PlatformMdns *m) { (void)m; }

PlatformTcpConn* platform_tcp_connect_async(const char *host, uint16_t port, platform_tcp_event_cb cb, void *cb_ctx) {
    (void)host; (void)port; (void)cb; (void)cb_ctx;
    return NULL;
}
int platform_tcp_send(PlatformTcpConn *conn, const uint8_t *data, size_t len) {
    (void)conn; (void)data; (void)len;
    return -1;
}
void platform_tcp_close(PlatformTcpConn *conn) { (void)conn; }

void platform_net_packet_release(void *packet_handle) { (void)packet_handle; }
