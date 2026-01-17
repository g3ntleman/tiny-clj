#include "platform.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#include <CoreFoundation/CoreFoundation.h>

// -----------------------------------------------------------------------------
// CFRunLoop-driven stdin buffering (keyboard input)
// -----------------------------------------------------------------------------

typedef struct {
    uint8_t buf[4096];
    size_t head;
    size_t tail;
    bool eof;
} StdinRing;

static StdinRing g_stdin_ring = {0};
static CFFileDescriptorRef g_stdin_fdref = NULL;
static CFRunLoopSourceRef g_stdin_source = NULL;

static inline size_t stdin_ring_count(const StdinRing *r) {
    if (r->tail >= r->head) return r->tail - r->head;
    return sizeof(r->buf) - (r->head - r->tail);
}

static inline bool stdin_ring_push(StdinRing *r, uint8_t b) {
    size_t next_tail = (r->tail + 1) % sizeof(r->buf);
    if (next_tail == r->head) {
        // Full.
        return false;
    }
    r->buf[r->tail] = b;
    r->tail = next_tail;
    return true;
}

static inline int stdin_ring_pop(StdinRing *r) {
    if (r->head == r->tail) return -1;
    uint8_t b = r->buf[r->head];
    r->head = (r->head + 1) % sizeof(r->buf);
    return (int)b;
}

static void stdin_read_cb(CFFileDescriptorRef fdref, CFOptionFlags callBackTypes, void *info) {
    (void)info;
    if (!fdref) return;
    if ((callBackTypes & kCFFileDescriptorReadCallBack) == 0) return;

    int fd = CFFileDescriptorGetNativeDescriptor(fdref);
    if (fd < 0) return;

    // Drain available bytes (non-blocking).
    for (;;) {
        uint8_t tmp[256];
        ssize_t n = read(fd, tmp, sizeof(tmp));
        if (n > 0) {
            for (ssize_t i = 0; i < n; i++) {
                (void)stdin_ring_push(&g_stdin_ring, tmp[i]);
            }
            continue;
        }
        if (n == 0) {
            // EOF.
            g_stdin_ring.eof = true;
            break;
        }
        // n < 0
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        // Treat other errors as EOF-equivalent for our input loop.
        g_stdin_ring.eof = true;
        break;
    }

    // Re-enable callbacks (one-shot).
    CFFileDescriptorEnableCallBacks(fdref, kCFFileDescriptorReadCallBack);
}

#ifdef ESP32_BUILD
#include "termios_stub.h"
#else
#include <termios.h>
#endif

void platform_init() {
    // Configure stdin as non-blocking and attach it to the current CFRunLoop.
    // This allows keyboard input to be delivered without polling.
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags != -1) {
        (void)fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }

    if (!g_stdin_fdref) {
        CFFileDescriptorContext ctx = {0};
        g_stdin_fdref = CFFileDescriptorCreate(kCFAllocatorDefault, STDIN_FILENO, false, stdin_read_cb, &ctx);
        if (g_stdin_fdref) {
            g_stdin_source = CFFileDescriptorCreateRunLoopSource(kCFAllocatorDefault, g_stdin_fdref, 0);
            if (g_stdin_source) {
                CFRunLoopAddSource(CFRunLoopGetCurrent(), g_stdin_source, kCFRunLoopDefaultMode);
                CFFileDescriptorEnableCallBacks(g_stdin_fdref, kCFFileDescriptorReadCallBack);
            }
        }
    }
}

void platform_sleep_ms(unsigned int ms) {
    // macOS/host: usleep expects microseconds.
    // Clamp to avoid overflow on very large ms values.
    unsigned int usec = (ms > (UINT_MAX / 1000u)) ? UINT_MAX : (ms * 1000u);
    usleep((useconds_t)usec);
}

void platform_runloop_run_once(unsigned int timeout_ms) {
    // Drive CFRunLoop sources (network sockets, timers, etc.) without blocking indefinitely.
    CFTimeInterval seconds = (CFTimeInterval)timeout_ms / 1000.0;
    (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, true);
}

void platform_print(const char *message) {
    if (!message) return;
    fputs(message, stdout);
    fputc('\n', stdout);
}

const char *platform_name() {
    return "macOS";
}

// -----------------------------------------------------------------------------
// Optional runtime stats (not available on host builds)
// -----------------------------------------------------------------------------
size_t platform_heap_bytes_free(void) { return (size_t)-1; }
size_t platform_heap_bytes_total(void) { return (size_t)-1; }
size_t platform_flash_bytes_free(void) { return (size_t)-1; }
size_t platform_flash_bytes_total(void) { return (size_t)-1; }

int platform_set_stdin_nonblocking(int enable) {
    // When stdin is integrated into CFRunLoop via CFFileDescriptor, the callback
    // drains stdin in a loop until it hits EAGAIN/EWOULDBLOCK. If stdin were put
    // back into blocking mode, that loop could block indefinitely after consuming
    // the currently available bytes, effectively freezing keyboard input.
    //
    // Therefore, on macOS we keep stdin non-blocking whenever CFRunLoop buffering
    // is active.
    if (g_stdin_fdref && !enable) {
        enable = 1;
    }
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
    
    // Consume from CFRunLoop-buffered stdin ring.
    static char linebuf[2048];
    static int len = 0;

    // Drain available bytes into linebuf.
    while (stdin_ring_count(&g_stdin_ring) > 0) {
        int c = stdin_ring_pop(&g_stdin_ring);
        if (c < 0) break;
        if (len + 1 >= (int)sizeof(linebuf)) {
            len = 0; // overflow protection
            return -1;
        }
        linebuf[len++] = (char)c;
        if ((char)c == '\n') break;
    }

    // EOF handling: if EOF and we have buffered bytes, flush them as a final line.
    if (g_stdin_ring.eof && len == 0) return -1;
    if (g_stdin_ring.eof && len > 0) {
        int outlen = (len < max - 1) ? len : (max - 1);
        // Strip trailing newline for readline-like behavior.
        if (outlen > 0 && linebuf[outlen - 1] == '\n') outlen--;
        memcpy(buf, linebuf, (size_t)outlen);
        buf[outlen] = '\0';
        len = 0;
        return outlen;
    }

    // Check for newline.
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
int platform_get_char(void *ctx) {
    (void)ctx;
    int b = stdin_ring_pop(&g_stdin_ring);
    if (b >= 0) return b;
    if (g_stdin_ring.eof) return -1;
    return -2; // no input available (non-blocking)
}

void platform_put_char(void *ctx, char c) {
    (void)ctx;
    putchar(c);
    fflush(stdout);
}

void platform_put_string(void *ctx, const char *s) {
    (void)ctx;
    if (s) {
        fputs(s, stdout);
    }
    fflush(stdout);
}

// Raw mode support for line editor
static bool raw_mode_enabled = false;

void platform_set_raw_mode(int enable) {
    static struct termios original_termios;
    
    if (enable && !raw_mode_enabled) {
        // Save original terminal settings
        tcgetattr(STDIN_FILENO, &original_termios);
        
        struct termios raw = original_termios;
        raw.c_lflag &= ~(ICANON | ECHO);
        // With VMIN=0/VTIME=0, read() may return 0 when no input is available,
        // which our CFRunLoop stdin callback previously interpreted as EOF.
        // Use VMIN=1 so "no data" yields EAGAIN/EWOULDBLOCK (since stdin is O_NONBLOCK).
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

// -----------------------------------------------------------------------------
// Networking (macOS) - CFRunLoop + CFSocket
// -----------------------------------------------------------------------------

struct PlatformUdpSocket {
    CFSocketRef sock;
    CFRunLoopSourceRef source;
    platform_udp_recv_cb cb;
    void *cb_ctx;
};

struct PlatformMdns {
    PlatformUdpSocket *v4;
    PlatformUdpSocket *v6;
};

static void udp_socket_cb(CFSocketRef s,
                          CFSocketCallBackType type,
                          CFDataRef address,
                          const void *data,
                          void *info) {
    (void)s;
    if (!info) return;
    if (type != kCFSocketDataCallBack) return;

    PlatformUdpSocket *u = (PlatformUdpSocket*)info;
    if (!u->cb) return;

    // Payload is provided as CFDataRef for kCFSocketDataCallBack.
    CFDataRef payload = (CFDataRef)data;
    if (!payload) return;

    // Extract source address.
    char addr_buf[INET6_ADDRSTRLEN];
    addr_buf[0] = '\0';
    uint16_t port = 0;
    if (address) {
        const UInt8 *ab = CFDataGetBytePtr(address);
        CFIndex alen = CFDataGetLength(address);
        if (ab && alen >= (CFIndex)sizeof(struct sockaddr)) {
            const struct sockaddr *sa = (const struct sockaddr*)ab;
            if (sa->sa_family == AF_INET && alen >= (CFIndex)sizeof(struct sockaddr_in)) {
                const struct sockaddr_in *sin = (const struct sockaddr_in*)sa;
                (void)inet_ntop(AF_INET, &sin->sin_addr, addr_buf, sizeof(addr_buf));
                port = ntohs(sin->sin_port);
            } else if (sa->sa_family == AF_INET6 && alen >= (CFIndex)sizeof(struct sockaddr_in6)) {
                const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6*)sa;
                (void)inet_ntop(AF_INET6, &sin6->sin6_addr, addr_buf, sizeof(addr_buf));
                port = ntohs(sin6->sin6_port);
            }
        }
    }

    const uint8_t *bytes = (const uint8_t*)CFDataGetBytePtr(payload);
    size_t len = (size_t)CFDataGetLength(payload);

    // packet_handle is a retained CFDataRef. Caller must release via platform_net_packet_release.
    CFRetain(payload);
    u->cb(u->cb_ctx, (void*)payload, bytes, len, addr_buf, port);
}

static PlatformUdpSocket* udp_wrap_native_fd(int fd, platform_udp_recv_cb cb, void *cb_ctx) {
    PlatformUdpSocket *u = (PlatformUdpSocket*)calloc(1, sizeof(PlatformUdpSocket));
    if (!u) {
        close(fd);
        return NULL;
    }
    u->cb = cb;
    u->cb_ctx = cb_ctx;

    CFSocketContext ctx = {0};
    ctx.info = u;

    CFSocketRef sock = CFSocketCreateWithNative(kCFAllocatorDefault,
                                                fd,
                                                kCFSocketDataCallBack,
                                                udp_socket_cb,
                                                &ctx);
    if (!sock) {
        close(fd);
        free(u);
        return NULL;
    }

    // Ensure we close the native fd when the socket is invalidated.
    CFOptionFlags flags = CFSocketGetSocketFlags(sock);
    flags |= kCFSocketCloseOnInvalidate;
    CFSocketSetSocketFlags(sock, flags);

    CFRunLoopSourceRef source = CFSocketCreateRunLoopSource(kCFAllocatorDefault, sock, 0);
    if (!source) {
        CFSocketInvalidate(sock);
        CFRelease(sock);
        free(u);
        return NULL;
    }
    CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode);

    u->sock = sock;
    u->source = source;
    return u;
}

PlatformUdpSocket* platform_udp_bind(uint16_t port, platform_udp_recv_cb cb, void *cb_ctx) {
    // Bind with native BSD sockets first so we can surface errors (e.g., sandbox EPERM).
    int fd = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return NULL;

    int yes = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, (socklen_t)sizeof(yes));

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_len = sizeof(sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr*)&sin, (socklen_t)sizeof(sin)) != 0) {
        close(fd);
        return NULL;
    }
    return udp_wrap_native_fd(fd, cb, cb_ctx);
}

int platform_udp_send(PlatformUdpSocket *sock,
                      const uint8_t *data, size_t len,
                      const char *to_addr, uint16_t to_port) {
    if (!sock || !sock->sock || !data || !to_addr) return -1;
    int fd = CFSocketGetNative(sock->sock);

    struct sockaddr_storage ss;
    memset(&ss, 0, sizeof(ss));

    // Prefer IPv4; fall back to IPv6 if parsing fails.
    struct sockaddr_in *sin = (struct sockaddr_in*)&ss;
    sin->sin_len = sizeof(struct sockaddr_in);
    sin->sin_family = AF_INET;
    sin->sin_port = htons(to_port);
    if (inet_pton(AF_INET, to_addr, &sin->sin_addr) == 1) {
        ssize_t n = sendto(fd, data, len, 0, (struct sockaddr*)sin, (socklen_t)sizeof(*sin));
        return (n < 0) ? -1 : 0;
    }

    struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)&ss;
    sin6->sin6_len = sizeof(struct sockaddr_in6);
    sin6->sin6_family = AF_INET6;
    sin6->sin6_port = htons(to_port);
    if (inet_pton(AF_INET6, to_addr, &sin6->sin6_addr) == 1) {
        ssize_t n = sendto(fd, data, len, 0, (struct sockaddr*)sin6, (socklen_t)sizeof(*sin6));
        return (n < 0) ? -1 : 0;
    }

    return -1;
}

void platform_udp_close(PlatformUdpSocket *sock) {
    if (!sock) return;
    if (sock->source) {
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), sock->source, kCFRunLoopDefaultMode);
        CFRelease(sock->source);
        sock->source = NULL;
    }
    if (sock->sock) {
        CFSocketInvalidate(sock->sock);
        CFRelease(sock->sock);
        sock->sock = NULL;
    }
    free(sock);
}

// -----------------------------------------------------------------------------
// mDNS transport (macOS)
// -----------------------------------------------------------------------------

PlatformMdns* platform_mdns_open(platform_udp_recv_cb cb, void *cb_ctx) {
    // Best-effort: create IPv4 + IPv6 sockets bound to 5353 with reuse enabled.
    const uint16_t port = 5353;

    PlatformMdns *m = (PlatformMdns*)calloc(1, sizeof(PlatformMdns));
    if (!m) return NULL;

    // IPv4
    {
        int fd = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd >= 0) {
            int yes = 1;
            (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, (socklen_t)sizeof(yes));
#ifdef SO_REUSEPORT
            (void)setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, (socklen_t)sizeof(yes));
#endif
            struct sockaddr_in sin;
            memset(&sin, 0, sizeof(sin));
            sin.sin_len = sizeof(sin);
            sin.sin_family = AF_INET;
            sin.sin_port = htons(port);
            sin.sin_addr.s_addr = htonl(INADDR_ANY);
            if (bind(fd, (struct sockaddr*)&sin, (socklen_t)sizeof(sin)) == 0) {
                // Join multicast group 224.0.0.251 on default interface.
                struct ip_mreq mreq;
                memset(&mreq, 0, sizeof(mreq));
                (void)inet_pton(AF_INET, "224.0.0.251", &mreq.imr_multiaddr);
                mreq.imr_interface.s_addr = htonl(INADDR_ANY);
                (void)setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, (socklen_t)sizeof(mreq));

                m->v4 = udp_wrap_native_fd(fd, cb, cb_ctx);
            } else {
                close(fd);
            }
        }
    }

    // IPv6
    {
        int fd = (int)socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
        if (fd >= 0) {
            int yes = 1;
            (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, (socklen_t)sizeof(yes));
#ifdef SO_REUSEPORT
            (void)setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, (socklen_t)sizeof(yes));
#endif
            // Keep it v6-only for predictable behavior.
            (void)setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, (socklen_t)sizeof(yes));

            struct sockaddr_in6 sin6;
            memset(&sin6, 0, sizeof(sin6));
            sin6.sin6_len = sizeof(sin6);
            sin6.sin6_family = AF_INET6;
            sin6.sin6_port = htons(port);
            sin6.sin6_addr = in6addr_any;
            if (bind(fd, (struct sockaddr*)&sin6, (socklen_t)sizeof(sin6)) == 0) {
                // Join ff02::fb (mDNS) on default interface index 0 (best-effort).
                struct ipv6_mreq mreq6;
                memset(&mreq6, 0, sizeof(mreq6));
                (void)inet_pton(AF_INET6, "ff02::fb", &mreq6.ipv6mr_multiaddr);
                mreq6.ipv6mr_interface = 0;
                (void)setsockopt(fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq6, (socklen_t)sizeof(mreq6));

                m->v6 = udp_wrap_native_fd(fd, cb, cb_ctx);
            } else {
                close(fd);
            }
        }
    }

    if (!m->v4 && !m->v6) {
        free(m);
        return NULL;
    }
    return m;
}

int platform_mdns_send_unicast(PlatformMdns *m,
                              const uint8_t *data, size_t len,
                              const char *to_addr, uint16_t to_port) {
    if (!m || !data || !to_addr) return -1;
    // Prefer IPv4 if parse succeeds, else IPv6.
    if (m->v4) {
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_len = sizeof(sin);
        sin.sin_family = AF_INET;
        sin.sin_port = htons(to_port);
        if (inet_pton(AF_INET, to_addr, &sin.sin_addr) == 1) {
            int fd = CFSocketGetNative(m->v4->sock);
            ssize_t n = sendto(fd, data, len, 0, (struct sockaddr*)&sin, (socklen_t)sizeof(sin));
            return (n < 0) ? -1 : 0;
        }
    }
    if (m->v6) {
        struct sockaddr_in6 sin6;
        memset(&sin6, 0, sizeof(sin6));
        sin6.sin6_len = sizeof(sin6);
        sin6.sin6_family = AF_INET6;
        sin6.sin6_port = htons(to_port);
        if (inet_pton(AF_INET6, to_addr, &sin6.sin6_addr) == 1) {
            int fd = CFSocketGetNative(m->v6->sock);
            ssize_t n = sendto(fd, data, len, 0, (struct sockaddr*)&sin6, (socklen_t)sizeof(sin6));
            return (n < 0) ? -1 : 0;
        }
    }
    return -1;
}

int platform_mdns_send_multicast(PlatformMdns *m, const uint8_t *data, size_t len) {
    if (!m || !data) return -1;
    int ok = 0;
    if (m->v4) {
        ok = (platform_mdns_send_unicast(m, data, len, "224.0.0.251", 5353) == 0) ? 1 : ok;
    }
    if (m->v6) {
        ok = (platform_mdns_send_unicast(m, data, len, "ff02::fb", 5353) == 0) ? 1 : ok;
    }
    return ok ? 0 : -1;
}

void platform_mdns_close(PlatformMdns *m) {
    if (!m) return;
    if (m->v4) { platform_udp_close(m->v4); m->v4 = NULL; }
    if (m->v6) { platform_udp_close(m->v6); m->v6 = NULL; }
    free(m);
}

// TCP is implemented in later todos (platform + builtins). For now, keep stubs that compile.
struct PlatformTcpConn {
    CFReadStreamRef r;
    CFWriteStreamRef w;
    platform_tcp_event_cb cb;
    void *cb_ctx;
    bool connected_emitted;
};

static void tcp_stream_cb(CFReadStreamRef stream, CFStreamEventType type, void *info) {
    if (!info) return;
    PlatformTcpConn *c = (PlatformTcpConn*)info;
    if (!c->cb) return;

    if (type == kCFStreamEventOpenCompleted) {
        // Emit connected once when we see an open on either stream.
        if (!c->connected_emitted) {
            c->connected_emitted = true;
            c->cb(c->cb_ctx, PLATFORM_TCP_EVENT_CONNECTED, NULL, NULL, 0);
        }
        return;
    }

    if (type == kCFStreamEventHasBytesAvailable) {
        uint8_t buf[2048];
        CFIndex n = CFReadStreamRead(stream, buf, (CFIndex)sizeof(buf));
        if (n > 0) {
            CFDataRef d = CFDataCreate(kCFAllocatorDefault, buf, n);
            if (!d) return;
            // packet_handle is retained CFDataRef.
            c->cb(c->cb_ctx, PLATFORM_TCP_EVENT_DATA, (void*)d,
                  (const uint8_t*)CFDataGetBytePtr(d), (size_t)CFDataGetLength(d));
            // Caller will release via platform_net_packet_release.
            return;
        }
        if (n == 0) {
            c->cb(c->cb_ctx, PLATFORM_TCP_EVENT_CLOSED, NULL, NULL, 0);
            return;
        }
        // n < 0 => error
        c->cb(c->cb_ctx, PLATFORM_TCP_EVENT_ERROR, NULL, NULL, 0);
        return;
    }

    if (type == kCFStreamEventEndEncountered) {
        c->cb(c->cb_ctx, PLATFORM_TCP_EVENT_CLOSED, NULL, NULL, 0);
        return;
    }

    if (type == kCFStreamEventErrorOccurred) {
        c->cb(c->cb_ctx, PLATFORM_TCP_EVENT_ERROR, NULL, NULL, 0);
        return;
    }
}

static void tcp_write_stream_cb(CFWriteStreamRef stream, CFStreamEventType type, void *info) {
    // We only need open/error/close notifications; data is read via read stream.
    (void)stream;
    if (!info) return;
    PlatformTcpConn *c = (PlatformTcpConn*)info;
    if (!c->cb) return;

    if (type == kCFStreamEventOpenCompleted) {
        if (!c->connected_emitted) {
            c->connected_emitted = true;
            c->cb(c->cb_ctx, PLATFORM_TCP_EVENT_CONNECTED, NULL, NULL, 0);
        }
        return;
    }
    if (type == kCFStreamEventEndEncountered) {
        c->cb(c->cb_ctx, PLATFORM_TCP_EVENT_CLOSED, NULL, NULL, 0);
        return;
    }
    if (type == kCFStreamEventErrorOccurred) {
        c->cb(c->cb_ctx, PLATFORM_TCP_EVENT_ERROR, NULL, NULL, 0);
        return;
    }
}

PlatformTcpConn* platform_tcp_connect_async(const char *host, uint16_t port,
                                            platform_tcp_event_cb cb, void *cb_ctx) {
    if (!host || !cb) return NULL;

    PlatformTcpConn *c = (PlatformTcpConn*)calloc(1, sizeof(PlatformTcpConn));
    if (!c) return NULL;
    c->cb = cb;
    c->cb_ctx = cb_ctx;
    c->connected_emitted = false;

    CFReadStreamRef r = NULL;
    CFWriteStreamRef w = NULL;
    CFStringRef host_str = CFStringCreateWithCString(kCFAllocatorDefault, host, kCFStringEncodingUTF8);
    if (!host_str) {
        free(c);
        return NULL;
    }

    CFStreamCreatePairWithSocketToHost(kCFAllocatorDefault, host_str, port, &r, &w);
    CFRelease(host_str);

    if (!r || !w) {
        if (r) CFRelease(r);
        if (w) CFRelease(w);
        free(c);
        return NULL;
    }

    // Set callbacks and schedule on current runloop.
    CFStreamClientContext ctx = {0};
    ctx.info = c;

    CFOptionFlags read_events = kCFStreamEventOpenCompleted |
                               kCFStreamEventHasBytesAvailable |
                               kCFStreamEventEndEncountered |
                               kCFStreamEventErrorOccurred;
    CFOptionFlags write_events = kCFStreamEventOpenCompleted |
                                kCFStreamEventEndEncountered |
                                kCFStreamEventErrorOccurred;

    if (!CFReadStreamSetClient(r, read_events, tcp_stream_cb, &ctx)) {
        CFRelease(r);
        CFRelease(w);
        free(c);
        return NULL;
    }
    if (!CFWriteStreamSetClient(w, write_events, tcp_write_stream_cb, &ctx)) {
        CFRelease(r);
        CFRelease(w);
        free(c);
        return NULL;
    }

    CFReadStreamScheduleWithRunLoop(r, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFWriteStreamScheduleWithRunLoop(w, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    c->r = r;
    c->w = w;

    (void)CFReadStreamOpen(r);
    (void)CFWriteStreamOpen(w);

    return c;
}

int platform_tcp_send(PlatformTcpConn *conn, const uint8_t *data, size_t len) {
    if (!conn || !conn->w || !data) return -1;
    if (len == 0) return 0;
    CFIndex n = CFWriteStreamWrite(conn->w, data, (CFIndex)len);
    return (n == (CFIndex)len) ? 0 : -1;
}

void platform_tcp_close(PlatformTcpConn *conn) {
    if (!conn) return;
    if (conn->r) {
        CFReadStreamUnscheduleFromRunLoop(conn->r, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        CFReadStreamClose(conn->r);
        CFRelease(conn->r);
        conn->r = NULL;
    }
    if (conn->w) {
        CFWriteStreamUnscheduleFromRunLoop(conn->w, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        CFWriteStreamClose(conn->w);
        CFRelease(conn->w);
        conn->w = NULL;
    }
    free(conn);
}

void platform_net_packet_release(void *packet_handle) {
    if (!packet_handle) return;
    CFDataRef d = (CFDataRef)packet_handle;
    CFRelease(d);
}
