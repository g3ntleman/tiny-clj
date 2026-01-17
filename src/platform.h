#ifndef TINY_CLJ_PLATFORM_H
#define TINY_CLJ_PLATFORM_H

// For size_t in stats APIs
#include <stddef.h>

void platform_init();
void platform_print(const char *message);
const char *platform_name();

// Sleep/delay helper used by clojure.core/sleep builtin.
// Contract: best-effort blocking delay for at least ms milliseconds.
void platform_sleep_ms(unsigned int ms);

// Run the platform event loop once, for up to timeout_ms milliseconds.
// On macOS this drives CFRunLoop sources (including network callbacks).
// On other platforms it may be a no-op.
void platform_runloop_run_once(unsigned int timeout_ms);

// Non-blocking input support for cooperative multitasking
// Enable/disable non-blocking mode for stdin (returns 0 on success)
int platform_set_stdin_nonblocking(int enable);
// Read a line non-blocking: returns number of bytes read into buf (null-terminated if >0),
// 0 if no complete line available yet, -1 on error. Max includes space for terminator.
int platform_readline_nb(char *buf, int max);

// Line editor platform functions
int platform_get_char(void *ctx);
void platform_put_char(void *ctx, char c);
void platform_put_string(void *ctx, const char *s);
void platform_set_raw_mode(int enable);

// -----------------------------------------------------------------------------
// Optional runtime stats (return SIZE_MAX if unavailable)
// -----------------------------------------------------------------------------
size_t platform_heap_bytes_free(void);
size_t platform_heap_bytes_total(void);
size_t platform_flash_bytes_free(void);
size_t platform_flash_bytes_total(void);

// -----------------------------------------------------------------------------
// Networking (UDP/TCP) - event-driven, zero-copy friendly
//
// Design goals:
// - No polling APIs (integrate with platform event mechanisms).
// - Avoid copying receive buffers: callback provides a packet_handle + (data,len).
// - Caller must eventually release packet_handle via platform_net_packet_release().
// -----------------------------------------------------------------------------
#include <stdint.h>

typedef struct PlatformUdpSocket PlatformUdpSocket;
typedef struct PlatformTcpConn PlatformTcpConn;
typedef struct PlatformMdns PlatformMdns;

typedef void (*platform_udp_recv_cb)(
    void *ctx,
    void *packet_handle,
    const uint8_t *data,
    size_t len,
    const char *from_addr,
    uint16_t from_port);

typedef enum PlatformTcpEvent {
    PLATFORM_TCP_EVENT_CONNECTED = 1,
    PLATFORM_TCP_EVENT_DATA = 2,
    PLATFORM_TCP_EVENT_CLOSED = 3,
    PLATFORM_TCP_EVENT_ERROR = 4,
} PlatformTcpEvent;

typedef void (*platform_tcp_event_cb)(
    void *ctx,
    PlatformTcpEvent event,
    void *packet_handle,
    const uint8_t *data,
    size_t len);

// UDP
PlatformUdpSocket* platform_udp_bind(uint16_t port, platform_udp_recv_cb cb, void *cb_ctx);
int platform_udp_send(PlatformUdpSocket *sock,
                      const uint8_t *data, size_t len,
                      const char *to_addr, uint16_t to_port);
void platform_udp_close(PlatformUdpSocket *sock);

// mDNS (multicast DNS) transport helpers.
// These are used by the DNS-SD resolver to send queries and receive responses.
// The callback uses the same signature as UDP receive callbacks.
PlatformMdns* platform_mdns_open(platform_udp_recv_cb cb, void *cb_ctx);
int platform_mdns_send_unicast(PlatformMdns *m,
                              const uint8_t *data, size_t len,
                              const char *to_addr, uint16_t to_port);
int platform_mdns_send_multicast(PlatformMdns *m, const uint8_t *data, size_t len);
void platform_mdns_close(PlatformMdns *m);

// TCP
PlatformTcpConn* platform_tcp_connect_async(const char *host, uint16_t port,
                                            platform_tcp_event_cb cb, void *cb_ctx);
int platform_tcp_send(PlatformTcpConn *conn, const uint8_t *data, size_t len);
void platform_tcp_close(PlatformTcpConn *conn);

// Release a packet_handle received via callbacks (e.g., return to pool / pbuf_free()).
void platform_net_packet_release(void *packet_handle);

#endif // TINY_CLJ_PLATFORM_H
