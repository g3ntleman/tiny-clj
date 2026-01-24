// ESP32 Platform functions for embedded execution (no REPL, no Line Editor)
#include "platform.h"
#include "memory.h" // CLJ_MALLOC/CLJ_FREE
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// -----------------------------------------------------------------------------
// Optional stdout observer hook (used by REPL to decide whether to print a newline
// before the next prompt).
// -----------------------------------------------------------------------------
__attribute__((weak)) void tinyclj_stdout_observe_bytes(const char *data, size_t n) {
    (void)data;
    (void)n;
}

#ifdef ESP32_BUILD
#if defined(__has_include)
#if __has_include(<lwip/udp.h>)
#define TINYCLJ_HAVE_LWIP 1
#include <lwip/udp.h>
#include <lwip/tcp.h>
#include <lwip/ip_addr.h>
#include <lwip/pbuf.h>
#include <lwip/err.h>
#else
#define TINYCLJ_HAVE_LWIP 0
#endif
#else
#define TINYCLJ_HAVE_LWIP 0
#endif
#endif

void platform_init(void) {
    // ESP32-specific initialization
}

// -----------------------------------------------------------------------------
// Sleep hook (override in ESP32/ESP-IDF integration)
// -----------------------------------------------------------------------------
__attribute__((weak)) void tinyclj_esp32_sleep_ms(unsigned int ms) { (void)ms; }

void platform_sleep_ms(unsigned int ms) {
    tinyclj_esp32_sleep_ms(ms);
}

void platform_runloop_run_once(unsigned int timeout_ms) {
    // Embedded build: no platform runloop integration here.
    // Best-effort sleep to avoid busy-waiting if callers use this API.
    platform_sleep_ms(timeout_ms);
}

void platform_print(const char *message) {
    if (!message) return;
    fputs(message, stdout);
    fputc('\n', stdout);
    tinyclj_stdout_observe_bytes(message, strlen(message));
    tinyclj_stdout_observe_bytes("\n", 1);
}

// -----------------------------------------------------------------------------
// Time hook (override in ESP32/ESP-IDF integration)
// -----------------------------------------------------------------------------
__attribute__((weak)) uint32_t tinyclj_esp32_current_time_ms(void) { return 0; }

uint32_t platform_current_time_ms(void) {
    return tinyclj_esp32_current_time_ms();
}

#if !defined(REPL_ENABLED) || (REPL_ENABLED == 0)
void platform_put_string(void *ctx, const char *s) {
    (void)ctx;
    if (s) {
        fputs(s, stdout);
        tinyclj_stdout_observe_bytes(s, strlen(s));
    }
}
#endif

const char *platform_name(void) {
    return "esp32";
}

// No line editor functions needed for embedded execution

__attribute__((weak)) bool platform_try_get_cursor_position(uint16_t *row, uint16_t *col) {
    (void)row;
    (void)col;
    return false;
}

// -----------------------------------------------------------------------------
// Optional runtime stats hooks (override in ESP32/ESP-IDF integration).
// Return SIZE_MAX if unknown/unavailable.
// -----------------------------------------------------------------------------
__attribute__((weak)) size_t tinyclj_esp32_heap_bytes_free(void) { return (size_t)-1; }
__attribute__((weak)) size_t tinyclj_esp32_heap_bytes_total(void) { return (size_t)-1; }
__attribute__((weak)) size_t tinyclj_esp32_flash_bytes_free(void) { return (size_t)-1; }
__attribute__((weak)) size_t tinyclj_esp32_flash_bytes_total(void) { return (size_t)-1; }

/*
 * ESP-IDF example: see the comment block in src/platform_esp32_uart.c
 * (you can override the same four functions for embedded builds).
 */

size_t platform_heap_bytes_free(void) { return tinyclj_esp32_heap_bytes_free(); }
size_t platform_heap_bytes_total(void) { return tinyclj_esp32_heap_bytes_total(); }
size_t platform_flash_bytes_free(void) { return tinyclj_esp32_flash_bytes_free(); }
size_t platform_flash_bytes_total(void) { return tinyclj_esp32_flash_bytes_total(); }

#if defined(ESP32_BUILD) && TINYCLJ_HAVE_LWIP

struct PlatformUdpSocket {
    struct udp_pcb *pcb;
    platform_udp_recv_cb cb;
    void *cb_ctx;
};

static void esp32_udp_recv(void *arg,
                           struct udp_pcb *pcb,
                           struct pbuf *p,
                           const ip_addr_t *addr,
                           u16_t port) {
    (void)pcb;
    PlatformUdpSocket *sock = (PlatformUdpSocket*)arg;
    if (!sock || !sock->cb || !p || !addr) {
        if (p) pbuf_free(p);
        return;
    }

    // Keep the pbuf alive after returning from lwIP callback:
    // - pbuf_ref increments refcount
    // - pbuf_free releases lwIP's reference
    pbuf_ref(p);
    pbuf_free(p);

    char addr_buf[48];
    addr_buf[0] = '\0';
    ipaddr_ntoa_r(addr, addr_buf, (int)sizeof(addr_buf));

    // Zero-copy path: only safe when data is contiguous in the first pbuf segment.
    // If the packet is chained, we fall back to a coalesced copy pbuf.
    struct pbuf *handle = p;
    const uint8_t *data = (const uint8_t*)p->payload;
    size_t len = (size_t)p->tot_len;
    if (p->len != p->tot_len) {
        struct pbuf *q = pbuf_alloc(PBUF_RAW, p->tot_len, PBUF_RAM);
        if (q) {
            pbuf_copy(q, p);
            // Drop our retained reference to the original chained pbuf.
            pbuf_free(p);
            handle = q;
            data = (const uint8_t*)q->payload;
            len = (size_t)q->tot_len;
        } else {
            // OOM: drop packet.
            pbuf_free(p);
            return;
        }
    }

    sock->cb(sock->cb_ctx, (void*)handle, data, len, addr_buf, (uint16_t)port);
}

PlatformUdpSocket* platform_udp_bind(uint16_t port, platform_udp_recv_cb cb, void *cb_ctx) {
    struct udp_pcb *pcb = udp_new();
    if (!pcb) return NULL;
    err_t e = udp_bind(pcb, IP_ANY_TYPE, port);
    if (e != ERR_OK) {
        udp_remove(pcb);
        return NULL;
    }

    PlatformUdpSocket *sock = (PlatformUdpSocket*)CLJ_MALLOC(sizeof(PlatformUdpSocket));
    if (!sock) {
        udp_remove(pcb);
        return NULL;
    }
    sock->pcb = pcb;
    sock->cb = cb;
    sock->cb_ctx = cb_ctx;
    udp_recv(pcb, esp32_udp_recv, sock);
    return sock;
}

int platform_udp_send(PlatformUdpSocket *sock,
                      const uint8_t *data, size_t len,
                      const char *to_addr, uint16_t to_port) {
    if (!sock || !sock->pcb || !data || !to_addr) return -1;

    ip_addr_t ip;
    if (!ipaddr_aton(to_addr, &ip)) return -1;

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
    if (!p) return -1;
    if (pbuf_take(p, data, (u16_t)len) != ERR_OK) {
        pbuf_free(p);
        return -1;
    }

    err_t e = udp_sendto(sock->pcb, p, &ip, to_port);
    pbuf_free(p);
    return (e == ERR_OK) ? 0 : -1;
}

void platform_udp_close(PlatformUdpSocket *sock) {
    if (!sock) return;
    if (sock->pcb) {
        udp_remove(sock->pcb);
        sock->pcb = NULL;
    }
    CLJ_FREE(sock);
}

struct PlatformTcpConn {
    struct tcp_pcb *pcb;
    platform_tcp_event_cb cb;
    void *cb_ctx;
};

static err_t esp32_tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    (void)err;
    PlatformTcpConn *c = (PlatformTcpConn*)arg;
    if (!c || !c->cb || !tpcb) {
        if (p) pbuf_free(p);
        return ERR_OK;
    }

    if (!p) {
        // Remote closed
        c->cb(c->cb_ctx, PLATFORM_TCP_EVENT_CLOSED, NULL, NULL, 0);
        return ERR_OK;
    }

    // Advertise to lwIP that we've received these bytes, immediately, to avoid needing
    // to call tcp_recved later from a different thread/task.
    tcp_recved(tpcb, p->tot_len);

    // Retain pbuf beyond callback and release lwIP's reference.
    pbuf_ref(p);
    pbuf_free(p);

    struct pbuf *handle = p;
    const uint8_t *data = (const uint8_t*)p->payload;
    size_t len = (size_t)p->tot_len;
    if (p->len != p->tot_len) {
        struct pbuf *q = pbuf_alloc(PBUF_RAW, p->tot_len, PBUF_RAM);
        if (q) {
            pbuf_copy(q, p);
            pbuf_free(p);
            handle = q;
            data = (const uint8_t*)q->payload;
            len = (size_t)q->tot_len;
        } else {
            pbuf_free(p);
            return ERR_OK;
        }
    }

    c->cb(c->cb_ctx, PLATFORM_TCP_EVENT_DATA, (void*)handle, data, len);
    return ERR_OK;
}

static void esp32_tcp_err(void *arg, err_t err) {
    (void)err;
    PlatformTcpConn *c = (PlatformTcpConn*)arg;
    if (c && c->cb) {
        c->cb(c->cb_ctx, PLATFORM_TCP_EVENT_ERROR, NULL, NULL, 0);
    }
    return;
}

static err_t esp32_tcp_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    (void)err;
    PlatformTcpConn *c = (PlatformTcpConn*)arg;
    if (!c || !c->cb || !tpcb) return ERR_OK;
    c->pcb = tpcb;
    tcp_recv(tpcb, esp32_tcp_recv);
    tcp_err(tpcb, esp32_tcp_err);
    tcp_arg(tpcb, c);
    c->cb(c->cb_ctx, PLATFORM_TCP_EVENT_CONNECTED, NULL, NULL, 0);
    return ERR_OK;
}

PlatformTcpConn* platform_tcp_connect_async(const char *host, uint16_t port,
                                            platform_tcp_event_cb cb, void *cb_ctx) {
    if (!host || !cb) return NULL;
    ip_addr_t ip;
    if (!ipaddr_aton(host, &ip)) return NULL;

    struct tcp_pcb *pcb = tcp_new_ip_type(IP_GET_TYPE(&ip));
    if (!pcb) return NULL;

    PlatformTcpConn *c = (PlatformTcpConn*)CLJ_MALLOC(sizeof(PlatformTcpConn));
    if (!c) {
        tcp_abort(pcb);
        return NULL;
    }
    c->pcb = pcb;
    c->cb = cb;
    c->cb_ctx = cb_ctx;

    tcp_arg(pcb, c);
    tcp_recv(pcb, esp32_tcp_recv);
    tcp_err(pcb, esp32_tcp_err);

    err_t e = tcp_connect(pcb, &ip, port, esp32_tcp_connected);
    if (e != ERR_OK) {
        tcp_abort(pcb);
        CLJ_FREE(c);
        return NULL;
    }

    return c;
}

int platform_tcp_send(PlatformTcpConn *conn, const uint8_t *data, size_t len) {
    if (!conn || !conn->pcb || !data) return -1;
    if (len == 0) return 0;
    err_t e = tcp_write(conn->pcb, data, (u16_t)len, TCP_WRITE_FLAG_COPY);
    if (e != ERR_OK) return -1;
    e = tcp_output(conn->pcb);
    return (e == ERR_OK) ? 0 : -1;
}

void platform_tcp_close(PlatformTcpConn *conn) {
    if (!conn) return;
    if (conn->pcb) {
        (void)tcp_close(conn->pcb);
        conn->pcb = NULL;
    }
    CLJ_FREE(conn);
}

void platform_net_packet_release(void *packet_handle) {
    if (!packet_handle) return;
    struct pbuf *p = (struct pbuf*)packet_handle;
    pbuf_free(p);
}

// -----------------------------------------------------------------------------
// mDNS transport (ESP32)
// -----------------------------------------------------------------------------
//
// Note: Proper dual-stack multicast join (IGMP/MLD) depends on lwIP/ESP-IDF
// integration details (netif, interface indices). For now we provide stubs that
// keep builds compiling; this will be implemented when the ESP32 runtime target
// is exercised end-to-end.

struct PlatformMdns { int unused; };

PlatformMdns* platform_mdns_open(platform_udp_recv_cb cb, void *cb_ctx) {
    (void)cb; (void)cb_ctx;
    return NULL;
}

int platform_mdns_send_unicast(PlatformMdns *m,
                              const uint8_t *data, size_t len,
                              const char *to_addr, uint16_t to_port) {
    (void)m; (void)data; (void)len; (void)to_addr; (void)to_port;
    return -1;
}

int platform_mdns_send_multicast(PlatformMdns *m, const uint8_t *data, size_t len) {
    (void)m; (void)data; (void)len;
    return -1;
}

void platform_mdns_close(PlatformMdns *m) { (void)m; }

#else  // !ESP32_BUILD || !TINYCLJ_HAVE_LWIP

PlatformUdpSocket* platform_udp_bind(uint16_t port, platform_udp_recv_cb cb, void *cb_ctx) {
    (void)port; (void)cb; (void)cb_ctx;
    return NULL;
}
int platform_udp_send(PlatformUdpSocket *sock,
                      const uint8_t *data, size_t len,
                      const char *to_addr, uint16_t to_port) {
    (void)sock; (void)data; (void)len; (void)to_addr; (void)to_port;
    return -1;
}
void platform_udp_close(PlatformUdpSocket *sock) { (void)sock; }

PlatformTcpConn* platform_tcp_connect_async(const char *host, uint16_t port,
                                            platform_tcp_event_cb cb, void *cb_ctx) {
    (void)host; (void)port; (void)cb; (void)cb_ctx;
    return NULL;
}
int platform_tcp_send(PlatformTcpConn *conn, const uint8_t *data, size_t len) {
    (void)conn; (void)data; (void)len;
    return -1;
}
void platform_tcp_close(PlatformTcpConn *conn) { (void)conn; }

void platform_net_packet_release(void *packet_handle) { (void)packet_handle; }

struct PlatformMdns { int unused; };
PlatformMdns* platform_mdns_open(platform_udp_recv_cb cb, void *cb_ctx) { (void)cb; (void)cb_ctx; return NULL; }
int platform_mdns_send_unicast(PlatformMdns *m, const uint8_t *data, size_t len, const char *to_addr, uint16_t to_port)
{ (void)m; (void)data; (void)len; (void)to_addr; (void)to_port; return -1; }
int platform_mdns_send_multicast(PlatformMdns *m, const uint8_t *data, size_t len) { (void)m; (void)data; (void)len; return -1; }
void platform_mdns_close(PlatformMdns *m) { (void)m; }

#endif // ESP32_BUILD && TINYCLJ_HAVE_LWIP



