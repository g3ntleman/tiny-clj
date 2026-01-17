#ifndef TINY_CLJ_MDNS_RESOLVER_H
#define TINY_CLJ_MDNS_RESOLVER_H

#include <stddef.h>
#include <stdint.h>

// Minimal browsing/resolve state machine (DNS-SD).
// This module is intentionally small and designed to be used without heap allocation
// if the caller provides fixed-capacity storage.

typedef enum MdnsEventType {
    MDNS_EVENT_INSTANCE_FOUND = 1,
    MDNS_EVENT_RESOLVED = 2,
    MDNS_EVENT_EXPIRED = 3,
} MdnsEventType;

typedef struct MdnsResolvedService {
    char instance[256];
    char service[256];
    char host[256];
    uint16_t port;
    // For now, we keep TXT raw bytes in a fixed buffer. Parsing into key/value can be added later.
    uint8_t txt[256];
    size_t txt_len;
    char addrs[4][64]; // up to 4 addresses (IPv4/IPv6 string)
    size_t addr_count;
} MdnsResolvedService;

typedef void (*mdns_event_cb)(void *ctx, MdnsEventType type, const MdnsResolvedService *svc);

typedef struct MdnsResolver MdnsResolver;

// Returns the required storage size for mdns_resolver_init().
size_t mdns_resolver_storage_size(void);

// Create a resolver using caller-provided storage.
// - storage must be a suitably aligned buffer; the resolver does not allocate heap memory.
MdnsResolver *mdns_resolver_init(void *storage, size_t storage_len, mdns_event_cb cb, void *cb_ctx);

// Start browsing a service type (e.g. "_matterc._udp.local").
int mdns_resolver_start_browse(MdnsResolver *r, const char *service_fullname);

// Feed an mDNS message (UDP payload). The resolver may emit events via callback.
int mdns_resolver_on_message(MdnsResolver *r, const uint8_t *msg, size_t msg_len);

// Advance time in milliseconds; used to expire TTL-based cache entries.
int mdns_resolver_tick(MdnsResolver *r, uint32_t now_ms);

#endif // TINY_CLJ_MDNS_RESOLVER_H

