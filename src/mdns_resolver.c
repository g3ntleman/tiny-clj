#include "mdns_resolver.h"

#include "mdns_codec.h"
#include "mini_format.h"

#include <string.h>

typedef struct MdnsEntry {
    char instance[256];
    char service[256];
    char host[256];
    uint16_t port;
    uint8_t txt[256];
    size_t txt_len;
    uint8_t addr6[16];
    int have_ptr;
    int have_srv;
    int have_txt;
    int have_addr6;
    int emitted_found;
    int emitted_resolved;
    uint32_t expires_at_ms;
} MdnsEntry;

typedef struct MdnsHostCache {
    char host[256];
    uint8_t addr6[16];
    int have_addr6;
    uint32_t expires_at_ms;
} MdnsHostCache;

struct MdnsResolver {
    mdns_event_cb cb;
    void *cb_ctx;
    char browse_service[256];
    uint32_t now_ms;

    MdnsEntry entries[8];
    MdnsHostCache hosts[8];
};

size_t mdns_resolver_storage_size(void) {
    return sizeof(MdnsResolver);
}

MdnsResolver *mdns_resolver_init(void *storage, size_t storage_len, mdns_event_cb cb, void *cb_ctx) {
    if (!storage || storage_len < sizeof(MdnsResolver)) return NULL;
    MdnsResolver *r = (MdnsResolver *)storage;
    memset(r, 0, sizeof(*r));
    r->cb = cb;
    r->cb_ctx = cb_ctx;
    return r;
}

static MdnsEntry *find_or_alloc_entry(MdnsResolver *r, const char *instance) {
    if (!r || !instance || instance[0] == '\0') return NULL;
    for (size_t i = 0; i < (sizeof(r->entries) / sizeof(r->entries[0])); i++) {
        if (r->entries[i].instance[0] != '\0' && strcmp(r->entries[i].instance, instance) == 0) {
            return &r->entries[i];
        }
    }
    for (size_t i = 0; i < (sizeof(r->entries) / sizeof(r->entries[0])); i++) {
        if (r->entries[i].instance[0] == '\0') {
            MdnsEntry *e = &r->entries[i];
            memset(e, 0, sizeof(*e));
            (void)mini_snprintf(e->instance, sizeof(e->instance), "%s", instance);
            return e;
        }
    }
    return NULL;
}

static MdnsHostCache *find_or_alloc_host(MdnsResolver *r, const char *host) {
    if (!r || !host || host[0] == '\0') return NULL;
    for (size_t i = 0; i < (sizeof(r->hosts) / sizeof(r->hosts[0])); i++) {
        if (r->hosts[i].host[0] != '\0' && strcmp(r->hosts[i].host, host) == 0) {
            return &r->hosts[i];
        }
    }
    for (size_t i = 0; i < (sizeof(r->hosts) / sizeof(r->hosts[0])); i++) {
        if (r->hosts[i].host[0] == '\0') {
            MdnsHostCache *h = &r->hosts[i];
            memset(h, 0, sizeof(*h));
            (void)mini_snprintf(h->host, sizeof(h->host), "%s", host);
            return h;
        }
    }
    return NULL;
}

static void format_ipv6_full(char *out, size_t out_sz, const uint8_t a[16]) {
    if (!out || out_sz == 0 || !a) return;
    out[0] = '\0';
    size_t off = 0;
    for (int i = 0; i < 8; i++) {
        uint16_t w = (uint16_t)((uint16_t)a[i * 2] << 8) | (uint16_t)a[i * 2 + 1];
        if (i == 0) {
            off += (size_t)mini_snprintf(out + off, (off < out_sz) ? (out_sz - off) : 0, "%x", (unsigned)w);
        } else {
            off += (size_t)mini_snprintf(out + off, (off < out_sz) ? (out_sz - off) : 0, ":%x", (unsigned)w);
        }
        if (off >= out_sz) break;
    }
}

int mdns_resolver_start_browse(MdnsResolver *r, const char *service_fullname) {
    if (!r || !service_fullname) return -1;
    (void)mini_snprintf(r->browse_service, sizeof(r->browse_service), "%s", service_fullname);
    return 0;
}

static int entry_try_emit_resolved(MdnsResolver *r, MdnsEntry *e) {
    if (!r || !e || !r->cb) return 0;
    if (e->emitted_resolved) return 0;
    if (!e->have_srv || !e->have_txt || !e->have_addr6) return 0;

    MdnsResolvedService svc;
    memset(&svc, 0, sizeof(svc));
    (void)mini_snprintf(svc.instance, sizeof(svc.instance), "%s", e->instance);
    (void)mini_snprintf(svc.service, sizeof(svc.service), "%s", e->service);
    (void)mini_snprintf(svc.host, sizeof(svc.host), "%s", e->host);
    svc.port = e->port;
    svc.txt_len = e->txt_len;
    if (svc.txt_len > sizeof(svc.txt)) svc.txt_len = sizeof(svc.txt);
    memcpy(svc.txt, e->txt, svc.txt_len);
    svc.addr_count = 1;
    format_ipv6_full(svc.addrs[0], sizeof(svc.addrs[0]), e->addr6);

    e->emitted_resolved = 1;
    r->cb(r->cb_ctx, MDNS_EVENT_RESOLVED, &svc);
    return 1;
}

int mdns_resolver_on_message(MdnsResolver *r, const uint8_t *msg, size_t msg_len) {
    if (!r || !msg) return -1;
    if (msg_len < 12) return -1;

    // Minimal DNS header parsing (network byte order).
    uint16_t qd = (uint16_t)((uint16_t)msg[4] << 8) | (uint16_t)msg[5];
    uint16_t an = (uint16_t)((uint16_t)msg[6] << 8) | (uint16_t)msg[7];
    uint16_t ns = (uint16_t)((uint16_t)msg[8] << 8) | (uint16_t)msg[9];
    uint16_t ar = (uint16_t)((uint16_t)msg[10] << 8) | (uint16_t)msg[11];

    size_t off = 12;

    // Skip questions (we don't need them yet).
    for (uint16_t i = 0; i < qd; i++) {
        char tmp[256];
        int rc = mdns_decode_name(msg, msg_len, &off, tmp, sizeof(tmp));
        if (rc != MDNS_CODEC_OK) return -1;
        if (off + 4 > msg_len) return -1;
        off += 4; // qtype + qclass
    }

    // Helper to parse a resource record section.
    uint16_t total_rr = (uint16_t)(an + ns + ar);
    for (uint16_t i = 0; i < total_rr; i++) {
        char rr_name[256];
        int rc = mdns_decode_name(msg, msg_len, &off, rr_name, sizeof(rr_name));
        if (rc != MDNS_CODEC_OK) return -1;
        if (off + 10 > msg_len) return -1;

        uint16_t type = (uint16_t)((uint16_t)msg[off + 0] << 8) | (uint16_t)msg[off + 1];
        // uint16_t class = ...
        uint32_t ttl = (uint32_t)((uint32_t)msg[off + 4] << 24) |
                       (uint32_t)((uint32_t)msg[off + 5] << 16) |
                       (uint32_t)((uint32_t)msg[off + 6] << 8) |
                       (uint32_t)msg[off + 7];
        uint16_t rdlen = (uint16_t)((uint16_t)msg[off + 8] << 8) | (uint16_t)msg[off + 9];
        off += 10;
        if (off + rdlen > msg_len) return -1;

        if (type == 12 /* PTR */ && r->browse_service[0] != '\0' && strcmp(rr_name, r->browse_service) == 0) {
            size_t rdata_off = off;
            char ptr_target[256];
            rc = mdns_decode_name(msg, msg_len, &rdata_off, ptr_target, sizeof(ptr_target));
            if (rc == MDNS_CODEC_OK) {
                MdnsEntry *e = find_or_alloc_entry(r, ptr_target);
                if (e) {
                    e->have_ptr = 1;
                    (void)mini_snprintf(e->service, sizeof(e->service), "%s", r->browse_service);
                    e->expires_at_ms = r->now_ms + (uint32_t)ttl * 1000u;
                    if (!e->emitted_found && r->cb) {
                        MdnsResolvedService svc;
                        memset(&svc, 0, sizeof(svc));
                        (void)mini_snprintf(svc.instance, sizeof(svc.instance), "%s", e->instance);
                        (void)mini_snprintf(svc.service, sizeof(svc.service), "%s", e->service);
                        e->emitted_found = 1;
                        r->cb(r->cb_ctx, MDNS_EVENT_INSTANCE_FOUND, &svc);
                    }
                }
            }
        } else if (type == 33 /* SRV */) {
            // RR name is the instance.
            if (rdlen < 6) { off += rdlen; continue; }
            MdnsEntry *e = find_or_alloc_entry(r, rr_name);
            if (e) {
                uint16_t port = (uint16_t)((uint16_t)msg[off + 4] << 8) | (uint16_t)msg[off + 5];
                size_t t_off = off + 6;
                char target[256];
                rc = mdns_decode_name(msg, msg_len, &t_off, target, sizeof(target));
                if (rc == MDNS_CODEC_OK) {
                    e->port = port;
                    (void)mini_snprintf(e->host, sizeof(e->host), "%s", target);
                    e->have_srv = 1;
                    e->expires_at_ms = r->now_ms + (uint32_t)ttl * 1000u;
                }
            }
        } else if (type == 16 /* TXT */) {
            MdnsEntry *e = find_or_alloc_entry(r, rr_name);
            if (e) {
                size_t n = rdlen;
                if (n > sizeof(e->txt)) n = sizeof(e->txt);
                memcpy(e->txt, msg + off, n);
                e->txt_len = n;
                e->have_txt = 1;
                e->expires_at_ms = r->now_ms + (uint32_t)ttl * 1000u;
            }
        } else if (type == 28 /* AAAA */) {
            if (rdlen == 16) {
                MdnsHostCache *h = find_or_alloc_host(r, rr_name);
                if (h) {
                    memcpy(h->addr6, msg + off, 16);
                    h->have_addr6 = 1;
                    h->expires_at_ms = r->now_ms + (uint32_t)ttl * 1000u;
                }
            }
        }

        off += rdlen;
    }

    // Try to attach host AAAA to entries and emit resolved if complete.
    for (size_t i = 0; i < (sizeof(r->entries) / sizeof(r->entries[0])); i++) {
        MdnsEntry *e = &r->entries[i];
        if (e->instance[0] == '\0') continue;
        if (!e->have_srv || e->host[0] == '\0') continue;
        if (!e->have_addr6) {
            for (size_t j = 0; j < (sizeof(r->hosts) / sizeof(r->hosts[0])); j++) {
                MdnsHostCache *h = &r->hosts[j];
                if (h->host[0] != '\0' && h->have_addr6 && strcmp(h->host, e->host) == 0) {
                    memcpy(e->addr6, h->addr6, 16);
                    e->have_addr6 = 1;
                    break;
                }
            }
        }
        (void)entry_try_emit_resolved(r, e);
    }

    return 0;
}

int mdns_resolver_tick(MdnsResolver *r, uint32_t now_ms) {
    if (!r) return -1;
    r->now_ms = now_ms;

    // Expire entries.
    for (size_t i = 0; i < (sizeof(r->entries) / sizeof(r->entries[0])); i++) {
        MdnsEntry *e = &r->entries[i];
        if (e->instance[0] == '\0') continue;
        if (e->expires_at_ms != 0 && now_ms > e->expires_at_ms) {
            if (r->cb) {
                MdnsResolvedService svc;
                memset(&svc, 0, sizeof(svc));
                (void)mini_snprintf(svc.instance, sizeof(svc.instance), "%s", e->instance);
                (void)mini_snprintf(svc.service, sizeof(svc.service), "%s", e->service);
                r->cb(r->cb_ctx, MDNS_EVENT_EXPIRED, &svc);
            }
            memset(e, 0, sizeof(*e));
        }
    }
    return 0;
}

