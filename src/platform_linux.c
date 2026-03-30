#include "platform.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <termios.h>
#include <poll.h>

__attribute__((weak)) void tinyclj_stdout_observe_bytes(const char *data, size_t n) {
    (void)data;
    (void)n;
}

void platform_init(void) {
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags != -1) {
        (void)fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }
}

void platform_sleep_ms(unsigned int ms) {
    usleep((useconds_t)(ms * 1000u));
}

void platform_runloop_run_once(unsigned int timeout_ms) {
    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
    (void)poll(&pfd, 1, (int)timeout_ms);
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

const char *platform_os_version(void) {
    static char buf[128];
    struct utsname u;
    if (uname(&u) == 0) {
        snprintf(buf, sizeof(buf), "%s", u.release);
        return buf;
    }
    return NULL;
}

size_t platform_heap_bytes_free(void)  { return (size_t)-1; }
size_t platform_heap_bytes_total(void) { return (size_t)-1; }
size_t platform_ram_bytes_total(void)  { return (size_t)-1; }
size_t platform_external_ram_bytes_total(void) { return (size_t)-1; }
size_t platform_flash_bytes_free(void)  { return (size_t)-1; }
size_t platform_flash_bytes_total(void) { return (size_t)-1; }

void platform_hardware_info(PlatformHardwareInfo *out) {
    if (out) {
        out->valid = false;
        out->model[0] = '\0';
        out->cores = 0;
        out->revision = 0;
        out->gpio_pin_count = 0;
        out->features = 0;
    }
}

int platform_set_stdin_nonblocking(int enable) {
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags == -1) return -1;
    if (enable) flags |= O_NONBLOCK; else flags &= ~O_NONBLOCK;
    return fcntl(STDIN_FILENO, F_SETFL, flags);
}

int platform_readline_nb(char *buf, int max) {
    if (!buf || max <= 1) return -1;
    if (max >= 10000) return 0;

    static char linebuf[2048];
    static int len = 0;
    static bool got_eof = false;

    while (len + 1 < (int)sizeof(linebuf)) {
        uint8_t ch;
        ssize_t n = read(STDIN_FILENO, &ch, 1);
        if (n == 1) {
            linebuf[len++] = (char)ch;
            if (ch == '\n') break;
        } else if (n == 0) {
            got_eof = true;
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            got_eof = true;
            break;
        }
    }

    if (got_eof && len == 0) return -1;
    if (got_eof && len > 0) {
        int outlen = (len < max - 1) ? len : (max - 1);
        if (outlen > 0 && linebuf[outlen - 1] == '\n') outlen--;
        memcpy(buf, linebuf, (size_t)outlen);
        buf[outlen] = '\0';
        len = 0;
        return outlen;
    }

    for (int i = 0; i < len; i++) {
        if (linebuf[i] == '\n') {
            int outlen = (i < max - 1) ? i : (max - 1);
            memcpy(buf, linebuf, (size_t)outlen);
            buf[outlen] = '\0';
            int remaining = len - (i + 1);
            memmove(linebuf, linebuf + i + 1, (size_t)remaining);
            len = remaining;
            return outlen;
        }
    }
    return 0;
}

int platform_get_char(void *ctx) {
    (void)ctx;
    uint8_t ch;
    ssize_t n = read(STDIN_FILENO, &ch, 1);
    if (n == 1) return (int)ch;
    if (n == 0) return -1;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return -2;
    return -1;
}

void platform_put_char(void *ctx, char c) {
    (void)ctx;
    putchar(c);
    fflush(stdout);
    tinyclj_stdout_observe_bytes(&c, 1);
}

void platform_put_string(void *ctx, const char *s) {
    (void)ctx;
    if (s) {
        fputs(s, stdout);
        tinyclj_stdout_observe_bytes(s, strlen(s));
    }
    fflush(stdout);
}

bool platform_try_get_cursor_position(uint16_t *row, uint16_t *col) {
    (void)row; (void)col;
    return false;
}

static bool raw_mode_enabled = false;

void platform_set_raw_mode(int enable) {
    static struct termios original_termios;

    if (enable && !raw_mode_enabled) {
        tcgetattr(STDIN_FILENO, &original_termios);
        struct termios raw = original_termios;
        raw.c_lflag &= ~(ICANON | ECHO | ISIG);
        raw.c_iflag &= ~(ICRNL | INLCR | IGNCR);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        raw_mode_enabled = true;
    } else if (!enable && raw_mode_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
        raw_mode_enabled = false;
    }
}

struct PlatformUdpSocket {
    int fd;
    platform_udp_recv_cb cb;
    void *cb_ctx;
};

struct PlatformMdns {
    PlatformUdpSocket *v4;
    PlatformUdpSocket *v6;
};

PlatformUdpSocket* platform_udp_bind(uint16_t port, platform_udp_recv_cb cb, void *cb_ctx) {
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return NULL;

    int yes = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, (socklen_t)sizeof(yes));

    int fl = fcntl(fd, F_GETFL, 0);
    if (fl != -1) (void)fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr*)&sin, sizeof(sin)) != 0) {
        close(fd);
        return NULL;
    }

    PlatformUdpSocket *u = (PlatformUdpSocket*)CLJ_CALLOC(1, sizeof(PlatformUdpSocket));
    u->fd = fd;
    u->cb = cb;
    u->cb_ctx = cb_ctx;
    return u;
}

int platform_udp_send(PlatformUdpSocket *sock,
                      const uint8_t *data, size_t len,
                      const char *to_addr, uint16_t to_port) {
    if (!sock || !data || !to_addr) return -1;

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(to_port);
    if (inet_pton(AF_INET, to_addr, &sin.sin_addr) == 1) {
        ssize_t n = sendto(sock->fd, data, len, 0, (struct sockaddr*)&sin, sizeof(sin));
        return (n < 0) ? -1 : 0;
    }

    struct sockaddr_in6 sin6;
    memset(&sin6, 0, sizeof(sin6));
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = htons(to_port);
    if (inet_pton(AF_INET6, to_addr, &sin6.sin6_addr) == 1) {
        ssize_t n = sendto(sock->fd, data, len, 0, (struct sockaddr*)&sin6, sizeof(sin6));
        return (n < 0) ? -1 : 0;
    }
    return -1;
}

void platform_udp_close(PlatformUdpSocket *sock) {
    if (!sock) return;
    if (sock->fd >= 0) close(sock->fd);
    CLJ_FREE(sock);
}

PlatformMdns* platform_mdns_open(platform_udp_recv_cb cb, void *cb_ctx) {
    PlatformMdns *m = (PlatformMdns*)CLJ_CALLOC(1, sizeof(PlatformMdns));
    m->v4 = platform_udp_bind(5353, cb, cb_ctx);
    if (!m->v4) {
        CLJ_FREE(m);
        return NULL;
    }
    return m;
}

int platform_mdns_send_unicast(PlatformMdns *m,
                              const uint8_t *data, size_t len,
                              const char *to_addr, uint16_t to_port) {
    if (!m || !m->v4) return -1;
    return platform_udp_send(m->v4, data, len, to_addr, to_port);
}

int platform_mdns_send_multicast(PlatformMdns *m, const uint8_t *data, size_t len) {
    if (!m || !m->v4) return -1;
    return platform_udp_send(m->v4, data, len, "224.0.0.251", 5353);
}

void platform_mdns_close(PlatformMdns *m) {
    if (!m) return;
    if (m->v4) { platform_udp_close(m->v4); m->v4 = NULL; }
    if (m->v6) { platform_udp_close(m->v6); m->v6 = NULL; }
    CLJ_FREE(m);
}

struct PlatformTcpConn {
    int fd;
    platform_tcp_event_cb cb;
    void *cb_ctx;
};

PlatformTcpConn* platform_tcp_connect_async(const char *host, uint16_t port,
                                            platform_tcp_event_cb cb, void *cb_ctx) {
    if (!host || !cb) return NULL;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return NULL;

    int fl = fcntl(fd, F_GETFL, 0);
    if (fl != -1) (void)fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &sin.sin_addr) != 1) {
        close(fd);
        return NULL;
    }

    (void)connect(fd, (struct sockaddr*)&sin, sizeof(sin));

    PlatformTcpConn *c = (PlatformTcpConn*)CLJ_CALLOC(1, sizeof(PlatformTcpConn));
    c->fd = fd;
    c->cb = cb;
    c->cb_ctx = cb_ctx;
    return c;
}

int platform_tcp_send(PlatformTcpConn *conn, const uint8_t *data, size_t len) {
    if (!conn || !data) return -1;
    if (len == 0) return 0;
    ssize_t n = send(conn->fd, data, len, 0);
    return (n == (ssize_t)len) ? 0 : -1;
}

void platform_tcp_close(PlatformTcpConn *conn) {
    if (!conn) return;
    if (conn->fd >= 0) close(conn->fd);
    CLJ_FREE(conn);
}

void platform_net_packet_release(void *packet_handle) {
    if (packet_handle) CLJ_FREE(packet_handle);
}
