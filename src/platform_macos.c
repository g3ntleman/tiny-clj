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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>

#include <CoreFoundation/CoreFoundation.h>
#include <malloc/malloc.h> // malloc_size

// -----------------------------------------------------------------------------
// Optional stdout observer hook (used by REPL to decide whether to print a newline
// before the next prompt).
// -----------------------------------------------------------------------------
__attribute__((weak)) void tinyclj_stdout_observe_bytes(const char *data, size_t n) {
    (void)data;
    (void)n;
}

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

// Bytes that we need to "push back" into the input stream (e.g. when probing terminal
// capabilities like DSR ESC[6n). platform_get_char/readline will serve these before g_stdin_ring.
static uint8_t g_inject_buf[128];
static size_t g_inject_len = 0;
static size_t g_inject_pos = 0;

static inline size_t inject_count(void) {
    return (g_inject_pos < g_inject_len) ? (g_inject_len - g_inject_pos) : 0;
}

static inline void inject_reset_if_empty(void) {
    if (g_inject_pos >= g_inject_len) {
        g_inject_pos = 0;
        g_inject_len = 0;
    }
}

static inline bool inject_append_bytes(const uint8_t *data, size_t n) {
    if (!data || n == 0) return true;
    inject_reset_if_empty();
    if (n > (sizeof(g_inject_buf) - g_inject_len)) return false;
    memcpy(&g_inject_buf[g_inject_len], data, n);
    g_inject_len += n;
    return true;
}

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
    tinyclj_stdout_observe_bytes(message, strlen(message));
    tinyclj_stdout_observe_bytes("\n", 1);
}

uint32_t platform_current_time_ms(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        return 0;
    }
    int32_t sec_in_day = (int32_t)(tv.tv_sec % 86400);
    if (sec_in_day < 0) sec_in_day = 0;
    int32_t millis = sec_in_day * 1000 + (int32_t)(tv.tv_usec / 1000);
    if (millis < 0) millis = 0;
    if (millis >= 86400000) millis = 86399999;
    return (uint32_t)millis;
}

const char *platform_name() {
    return "macOS";
}

// -----------------------------------------------------------------------------
// Optional runtime stats (not available on host builds)
// -----------------------------------------------------------------------------
size_t platform_heap_bytes_free(void) { return (size_t)-1; }
size_t platform_heap_bytes_total(void) { return (size_t)-1; }
size_t platform_ram_bytes_total(void) { return (size_t)-1; }
size_t platform_flash_bytes_free(void) { return (size_t)-1; }
size_t platform_flash_bytes_total(void) { return (size_t)-1; }

void platform_hardware_info(PlatformHardwareInfo *out) {
    if (out) out->valid = false;
}

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

    // Drain available bytes into linebuf (including injected bytes).
    while (inject_count() > 0 || stdin_ring_count(&g_stdin_ring) > 0) {
        int c = (inject_count() > 0) ? (int)g_inject_buf[g_inject_pos++] : stdin_ring_pop(&g_stdin_ring);
        inject_reset_if_empty();
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
    if (inject_count() > 0) {
        int b = (int)g_inject_buf[g_inject_pos++];
        inject_reset_if_empty();
        return b;
    }
    int b = stdin_ring_pop(&g_stdin_ring);
    if (b >= 0) return b;
    if (g_stdin_ring.eof) return -1;
    return -2; // no input available (non-blocking)
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
    if (!row || !col) return false;
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return false;
    const char *term = getenv("TERM");
    if (term && strcmp(term, "dumb") == 0) return false;

    // Avoid interfering with real user input: only probe when input buffers are empty.
    if (inject_count() > 0 || stdin_ring_count(&g_stdin_ring) > 0) return false;

    // Send DSR query ESC[6n without triggering stdout observer heuristics.
    const char q[] = "\033[6n";
    (void)fwrite(q, 1, sizeof(q) - 1, stdout);
    fflush(stdout);

    uint8_t captured[64];
    size_t cap_len = 0;

    // Parse states: expect ESC '[' row ';' col 'R'
    enum { S_ESC, S_BRACKET, S_ROW, S_COL, S_DONE } st = S_ESC;
    unsigned int r = 0, c = 0;

    // Wait up to ~25ms total, pumping CFRunLoop to receive bytes into g_stdin_ring.
    for (unsigned int tries = 0; tries < 25; tries++) {
        platform_runloop_run_once(1);

        while (stdin_ring_count(&g_stdin_ring) > 0 && cap_len < sizeof(captured)) {
            int bi = stdin_ring_pop(&g_stdin_ring);
            if (bi < 0) break;
            uint8_t b = (uint8_t)bi;
            captured[cap_len++] = b;

            switch (st) {
                case S_ESC:
                    if (b == 0x1b) st = S_BRACKET;
                    else st = S_DONE;
                    break;
                case S_BRACKET:
                    if (b == (uint8_t)'[') st = S_ROW;
                    else st = S_DONE;
                    break;
                case S_ROW:
                    if (b >= '0' && b <= '9') { r = (r * 10u) + (unsigned)(b - '0'); }
                    else if (b == ';') st = S_COL;
                    else st = S_DONE;
                    break;
                case S_COL:
                    if (b >= '0' && b <= '9') { c = (c * 10u) + (unsigned)(b - '0'); }
                    else if (b == 'R') st = S_DONE;
                    else st = S_DONE;
                    break;
                default:
                    break;
            }

            if (st == S_DONE) {
                // Accept only if we ended on 'R' and parsed sane numbers.
                if (cap_len >= 6 && captured[0] == 0x1b && captured[1] == '[' && r > 0 && c > 0 && captured[cap_len - 1] == 'R') {
                    *row = (uint16_t)r;
                    *col = (uint16_t)c;
                    return true;
                }
                // Not a valid DSR response: push back so we don't lose user input.
                (void)inject_append_bytes(captured, cap_len);
                return false;
            }
        }
    }

    // Timeout: if we captured anything, push it back.
    if (cap_len > 0) (void)inject_append_bytes(captured, cap_len);
    return false;
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
        // Preserve carriage return in raw mode so Enter is not translated to '\n'.
        raw.c_iflag &= ~(ICRNL | INLCR | IGNCR);
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
    // We use kCFSocketReadCallBack and perform recvfrom() ourselves for robustness.
    if (type != kCFSocketReadCallBack) return;
    (void)address;
    (void)data;

    PlatformUdpSocket *u = (PlatformUdpSocket*)info;
    if (!u->cb) return;

    int fd = CFSocketGetNative(u->sock);
    if (fd < 0) return;

    // Read as many datagrams as are available (socket is non-blocking).
    for (;;) {
        struct sockaddr_storage ss;
        socklen_t sslen = (socklen_t)sizeof(ss);
        memset(&ss, 0, sizeof(ss));

        // RFC6762 says mDNS messages should fit into a single UDP datagram.
        // Keep this modest but safe for typical Bonjour payloads.
        uint8_t buf[4096];
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr*)&ss, &sslen);
        if (n <= 0) {
            // EAGAIN/EWOULDBLOCK => no more packets.
            break;
        }

        // Extract source address string + port.
    char addr_buf[INET6_ADDRSTRLEN];
    addr_buf[0] = '\0';
    uint16_t port = 0;
        const struct sockaddr *sa = (const struct sockaddr*)&ss;
        if (sa->sa_family == AF_INET && sslen >= (socklen_t)sizeof(struct sockaddr_in)) {
                const struct sockaddr_in *sin = (const struct sockaddr_in*)sa;
                (void)inet_ntop(AF_INET, &sin->sin_addr, addr_buf, sizeof(addr_buf));
                port = ntohs(sin->sin_port);
        } else if (sa->sa_family == AF_INET6 && sslen >= (socklen_t)sizeof(struct sockaddr_in6)) {
                const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6*)sa;
                (void)inet_ntop(AF_INET6, &sin6->sin6_addr, addr_buf, sizeof(addr_buf));
                port = ntohs(sin6->sin6_port);
            }

        const char *trace = getenv("TINYCLJ_MDNS_TRACE");
        if (trace && trace[0] != '\0' && trace[0] != '0') {
            fprintf(stderr, "[tiny-clj mdns] recv %zd bytes from %s:%u\n",
                    n,
                    addr_buf[0] ? addr_buf : "?",
                    (unsigned)port);
        }

        // Copy bytes into a CFDataRef so the callback can treat it as a zero-copy packet handle.
        CFDataRef payload = CFDataCreate(kCFAllocatorDefault, buf, (CFIndex)n);
        if (!payload) continue;

    const uint8_t *bytes = (const uint8_t*)CFDataGetBytePtr(payload);
    size_t len = (size_t)CFDataGetLength(payload);

    // packet_handle is a retained CFDataRef. Caller must release via platform_net_packet_release.
    u->cb(u->cb_ctx, (void*)payload, bytes, len, addr_buf, port);

        // If the callback didn't take ownership (it should), release defensively would be wrong.
        // Ownership contract: platform_net_packet_release() will CFRelease(payload).
        // So we do not CFRelease here.
    }
}

static PlatformUdpSocket* udp_wrap_native_fd(int fd, platform_udp_recv_cb cb, void *cb_ctx) {
    PlatformUdpSocket *u = (PlatformUdpSocket*)CLJ_CALLOC(1, sizeof(PlatformUdpSocket));
    u->cb = cb;
    u->cb_ctx = cb_ctx;

    // Ensure non-blocking so the CFRunLoop callback can drain recvfrom() without hanging.
    int fd_flags = fcntl(fd, F_GETFL, 0);
    if (fd_flags != -1) {
        (void)fcntl(fd, F_SETFL, fd_flags | O_NONBLOCK);
    }

    CFSocketContext ctx = {0};
    ctx.info = u;

    CFSocketRef sock = CFSocketCreateWithNative(kCFAllocatorDefault,
                                                fd,
                                                kCFSocketReadCallBack,
                                                udp_socket_cb,
                                                &ctx);
    if (!sock) {
        close(fd);
        CLJ_FREE(u);
        return NULL;
    }

    // Ensure we close the native fd when the socket is invalidated.
    CFOptionFlags sock_flags = CFSocketGetSocketFlags(sock);
    sock_flags |= kCFSocketCloseOnInvalidate;
    sock_flags |= kCFSocketAutomaticallyReenableReadCallBack;
    CFSocketSetSocketFlags(sock, sock_flags);

    CFRunLoopSourceRef source = CFSocketCreateRunLoopSource(kCFAllocatorDefault, sock, 0);
    if (!source) {
        CFSocketInvalidate(sock);
        CFRelease(sock);
        CLJ_FREE(u);
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
    CLJ_FREE(sock);
}

// -----------------------------------------------------------------------------
// mDNS transport (macOS)
// -----------------------------------------------------------------------------

PlatformMdns* platform_mdns_open(platform_udp_recv_cb cb, void *cb_ctx) {
    // Best-effort: create IPv4 + IPv6 sockets bound to 5353 with reuse enabled.
    const uint16_t port = 5353;

    PlatformMdns *m = (PlatformMdns*)CLJ_CALLOC(1, sizeof(PlatformMdns));

    // Enumerate interfaces so we can join IPv4/IPv6 mDNS multicast groups on all
    // multicast-capable interfaces. This matters on hosts with VPNs / multiple NICs.
    struct ifaddrs *ifas = NULL;
    (void)getifaddrs(&ifas);

    // IPv4
    {
        int fd = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd >= 0) {
            int yes = 1;
            (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, (socklen_t)sizeof(yes));
#ifdef SO_REUSEPORT
            (void)setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, (socklen_t)sizeof(yes));
#endif
            // RFC 6762: mDNS messages must be sent with IP TTL = 255.
            // Many responders ignore packets with other TTL values.
            {
                int ttl = 255;
                (void)setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, (socklen_t)sizeof(ttl));
                (void)setsockopt(fd, IPPROTO_IP, IP_TTL, &ttl, (socklen_t)sizeof(ttl));
                (void)setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &yes, (socklen_t)sizeof(yes));
            }
            struct sockaddr_in sin;
            memset(&sin, 0, sizeof(sin));
            sin.sin_len = sizeof(sin);
            sin.sin_family = AF_INET;
            sin.sin_port = htons(port);
            sin.sin_addr.s_addr = htonl(INADDR_ANY);
            if (bind(fd, (struct sockaddr*)&sin, (socklen_t)sizeof(sin)) == 0) {
                // Join multicast group 224.0.0.251 on all multicast-capable IPv4 interfaces.
                int joined_any = 0;
                if (ifas) {
                    for (struct ifaddrs *ifa = ifas; ifa; ifa = ifa->ifa_next) {
                        if (!ifa->ifa_addr) continue;
                        if ((ifa->ifa_flags & IFF_UP) == 0) continue;
                        if ((ifa->ifa_flags & IFF_MULTICAST) == 0) continue;
                        if ((ifa->ifa_flags & IFF_LOOPBACK) != 0) continue;
                        if (ifa->ifa_addr->sa_family != AF_INET) continue;

                        struct ip_mreq mreq;
                        memset(&mreq, 0, sizeof(mreq));
                        (void)inet_pton(AF_INET, "224.0.0.251", &mreq.imr_multiaddr);
                        mreq.imr_interface = ((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
                        if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, (socklen_t)sizeof(mreq)) == 0) {
                            joined_any = 1;
                        }
                    }
                }
                if (!joined_any) {
                    // Fallback: best-effort join on default interface.
                    struct ip_mreq mreq;
                    memset(&mreq, 0, sizeof(mreq));
                    (void)inet_pton(AF_INET, "224.0.0.251", &mreq.imr_multiaddr);
                    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
                    (void)setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, (socklen_t)sizeof(mreq));
                }

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
            // RFC 6762: mDNS messages must be sent with IPv6 hop limit = 255.
            {
                int hops = 255;
                (void)setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, (socklen_t)sizeof(hops));
                (void)setsockopt(fd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &hops, (socklen_t)sizeof(hops));
                (void)setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &yes, (socklen_t)sizeof(yes));
            }

            struct sockaddr_in6 sin6;
            memset(&sin6, 0, sizeof(sin6));
            sin6.sin6_len = sizeof(sin6);
            sin6.sin6_family = AF_INET6;
            sin6.sin6_port = htons(port);
            sin6.sin6_addr = in6addr_any;
            if (bind(fd, (struct sockaddr*)&sin6, (socklen_t)sizeof(sin6)) == 0) {
                // Join ff02::fb (mDNS) on all multicast-capable IPv6 interfaces.
                int joined_any = 0;
                if (ifas) {
                    for (struct ifaddrs *ifa = ifas; ifa; ifa = ifa->ifa_next) {
                        if (!ifa->ifa_addr) continue;
                        if ((ifa->ifa_flags & IFF_UP) == 0) continue;
                        if ((ifa->ifa_flags & IFF_MULTICAST) == 0) continue;
                        if ((ifa->ifa_flags & IFF_LOOPBACK) != 0) continue;
                        if (ifa->ifa_addr->sa_family != AF_INET6) continue;

                        unsigned int ifindex = if_nametoindex(ifa->ifa_name);
                        if (ifindex == 0) continue;

                        struct ipv6_mreq mreq6;
                        memset(&mreq6, 0, sizeof(mreq6));
                        (void)inet_pton(AF_INET6, "ff02::fb", &mreq6.ipv6mr_multiaddr);
                        mreq6.ipv6mr_interface = ifindex;
                        if (setsockopt(fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq6, (socklen_t)sizeof(mreq6)) == 0) {
                            joined_any = 1;
                        }
                    }
                }
                if (!joined_any) {
                    // Fallback: best-effort (may not work for link-local multicast).
                    struct ipv6_mreq mreq6;
                    memset(&mreq6, 0, sizeof(mreq6));
                    (void)inet_pton(AF_INET6, "ff02::fb", &mreq6.ipv6mr_multiaddr);
                    mreq6.ipv6mr_interface = 0;
                    (void)setsockopt(fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq6, (socklen_t)sizeof(mreq6));
                }

                m->v6 = udp_wrap_native_fd(fd, cb, cb_ctx);
            } else {
                close(fd);
            }
        }
    }

    if (ifas) {
        freeifaddrs(ifas);
    }

    if (!m->v4 && !m->v6) {
        CLJ_FREE(m);
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
        // IPv4 multicast queries should go out on the LAN interface. If the host has a VPN
        // as default route, sending on "default" may not reach the local network.
        // Send once per multicast-capable interface (best-effort).
        struct ifaddrs *ifas = NULL;
        (void)getifaddrs(&ifas);
        if (ifas) {
            for (struct ifaddrs *ifa = ifas; ifa; ifa = ifa->ifa_next) {
                if (!ifa->ifa_addr) continue;
                if ((ifa->ifa_flags & IFF_UP) == 0) continue;
                if ((ifa->ifa_flags & IFF_MULTICAST) == 0) continue;
                if ((ifa->ifa_flags & IFF_LOOPBACK) != 0) continue;
                if (ifa->ifa_addr->sa_family != AF_INET) continue;

                struct in_addr ifaddr = ((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
                (void)setsockopt(CFSocketGetNative(m->v4->sock), IPPROTO_IP, IP_MULTICAST_IF,
                                 &ifaddr, (socklen_t)sizeof(ifaddr));

                struct sockaddr_in sin;
                memset(&sin, 0, sizeof(sin));
                sin.sin_len = sizeof(sin);
                sin.sin_family = AF_INET;
                sin.sin_port = htons(5353);
                (void)inet_pton(AF_INET, "224.0.0.251", &sin.sin_addr);
                ssize_t n = sendto(CFSocketGetNative(m->v4->sock), data, len, 0,
                                   (struct sockaddr*)&sin, (socklen_t)sizeof(sin));
                if (n >= 0) ok = 1;
            }
            freeifaddrs(ifas);
        } else {
            ok = (platform_mdns_send_unicast(m, data, len, "224.0.0.251", 5353) == 0) ? 1 : ok;
        }
    }
    if (m->v6) {
        // For IPv6 link-local multicast, a scope (interface index) is required.
        // Send once per multicast-capable interface (best-effort).
        struct ifaddrs *ifas = NULL;
        (void)getifaddrs(&ifas);
        if (ifas) {
            for (struct ifaddrs *ifa = ifas; ifa; ifa = ifa->ifa_next) {
                if (!ifa->ifa_addr) continue;
                if ((ifa->ifa_flags & IFF_UP) == 0) continue;
                if ((ifa->ifa_flags & IFF_MULTICAST) == 0) continue;
                if ((ifa->ifa_flags & IFF_LOOPBACK) != 0) continue;
                if (ifa->ifa_addr->sa_family != AF_INET6) continue;

                unsigned int ifindex = if_nametoindex(ifa->ifa_name);
                if (ifindex == 0) continue;

                struct sockaddr_in6 sin6;
                memset(&sin6, 0, sizeof(sin6));
                sin6.sin6_len = sizeof(sin6);
                sin6.sin6_family = AF_INET6;
                sin6.sin6_port = htons(5353);
                sin6.sin6_scope_id = ifindex;
                if (inet_pton(AF_INET6, "ff02::fb", &sin6.sin6_addr) != 1) continue;

                int fd = CFSocketGetNative(m->v6->sock);
                ssize_t n = sendto(fd, data, len, 0, (struct sockaddr*)&sin6, (socklen_t)sizeof(sin6));
                if (n >= 0) ok = 1;
            }
            freeifaddrs(ifas);
        } else {
            // Fallback (may fail if scope is required).
            ok = (platform_mdns_send_unicast(m, data, len, "ff02::fb", 5353) == 0) ? 1 : ok;
        }
    }
    return ok ? 0 : -1;
}

void platform_mdns_close(PlatformMdns *m) {
    if (!m) return;
    if (m->v4) { platform_udp_close(m->v4); m->v4 = NULL; }
    if (m->v6) { platform_udp_close(m->v6); m->v6 = NULL; }
    CLJ_FREE(m);
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

    PlatformTcpConn *c = (PlatformTcpConn*)CLJ_CALLOC(1, sizeof(PlatformTcpConn));
    c->cb = cb;
    c->cb_ctx = cb_ctx;
    c->connected_emitted = false;

    CFReadStreamRef r = NULL;
    CFWriteStreamRef w = NULL;
    CFStringRef host_str = CFStringCreateWithCString(kCFAllocatorDefault, host, kCFStringEncodingUTF8);
    if (!host_str) {
        CLJ_FREE(c);
        return NULL;
    }

    CFStreamCreatePairWithSocketToHost(kCFAllocatorDefault, host_str, port, &r, &w);
    CFRelease(host_str);

    if (!r || !w) {
        if (r) CFRelease(r);
        if (w) CFRelease(w);
        CLJ_FREE(c);
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
        CLJ_FREE(c);
        return NULL;
    }
    if (!CFWriteStreamSetClient(w, write_events, tcp_write_stream_cb, &ctx)) {
        CFRelease(r);
        CFRelease(w);
        CLJ_FREE(c);
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
    CLJ_FREE(conn);
}

void platform_net_packet_release(void *packet_handle) {
    if (!packet_handle) return;
    CFDataRef d = (CFDataRef)packet_handle;
    CFRelease(d);
}
