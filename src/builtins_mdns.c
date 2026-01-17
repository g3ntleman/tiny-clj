/**
 * builtins_mdns.c - Native bindings for tinyclj.net.mdns
 *
 * Browsing/resolve only (no advertise). The implementation is callback-driven and
 * designed to keep RAM usage low:
 * - No sockets or resolver state are created until mdns/open is called.
 * - Receive buffers are zero-copy via platform_net_packet_release().
 */

#include "builtins.h"
#include "byte_array.h"
#include "exception.h"  // CHECK_ARITY + TRY/CATCH
#include "eval.h"
#include "function.h"
#include "map.h"
#include "memory.h"
#include "namespace.h"
#include "platform.h"
#include "strings.h"
#include "symbol.h"
#include "value.h"
#include "vector.h"

#include "mdns_codec.h"
#include "mdns_resolver.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __APPLE__
#include <dns_sd.h>
#include <CoreFoundation/CoreFoundation.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdlib.h>
#endif

#ifdef __APPLE__
struct MdnsCtx;
typedef struct DnssdOp {
    int in_use;
    size_t slot_index;
    struct MdnsCtx *owner;
    char service_name[256];
    char regtype[256];
    char domain[256];
    char host[256];
    uint16_t port;
    uint8_t txt[256];
    size_t txt_len;
    char addrs[4][INET6_ADDRSTRLEN];
    size_t addr_count;
    int got_resolve;
    int got_addr_done;

    DNSServiceRef resolve_ref;
    CFFileDescriptorRef resolve_fdref;
    CFRunLoopSourceRef resolve_source;

    DNSServiceRef addr_ref;
    CFFileDescriptorRef addr_fdref;
    CFRunLoopSourceRef addr_source;
} DnssdOp;
#endif

typedef struct MdnsCtx {
    PlatformMdns *m;
    MdnsResolver *resolver;
    void *resolver_storage;
    ID on_event_fn; // retained, may be NULL
    uint8_t *handle_bytes;

#ifdef __APPLE__
    // macOS: optionally use Apple's DNS-SD API (mDNSResponder) instead of raw UDP.
    int use_dnssd;
    char dnssd_browse_service[256];
    DNSServiceRef dnssd_browse_ref;
    CFFileDescriptorRef dnssd_browse_fdref;
    CFRunLoopSourceRef dnssd_browse_source;

    DnssdOp dnssd_ops[8];
#endif
} MdnsCtx;

static ID kw_instance_found = NULL;
static ID kw_resolved = NULL;
static ID kw_expired = NULL;
static ID kw_instance = NULL;
static ID kw_service = NULL;
static ID kw_addrs = NULL;
static ID kw_txt = NULL;

static void mdns_init_keywords(void) {
    if (kw_instance_found) return;
    kw_instance_found = (ID)intern_symbol_global(":instance-found");
    kw_resolved = (ID)intern_symbol_global(":resolved");
    kw_expired = (ID)intern_symbol_global(":expired");
    kw_instance = (ID)intern_symbol_global(":instance");
    kw_service = (ID)intern_symbol_global(":service");
    kw_addrs = (ID)intern_symbol_global(":addrs");
    kw_txt = (ID)intern_symbol_global(":txt");
}

static void mdns_ctx_free(void *ctx) {
    MdnsCtx *m = (MdnsCtx*)ctx;
    if (!m) return;

#ifdef __APPLE__
    if (m->use_dnssd) {
        // Tear down outstanding resolve/address ops.
        for (size_t i = 0; i < (sizeof(m->dnssd_ops) / sizeof(m->dnssd_ops[0])); i++) {
            if (!m->dnssd_ops[i].in_use) continue;
            if (m->dnssd_ops[i].resolve_source) {
                CFRunLoopRemoveSource(CFRunLoopGetCurrent(), m->dnssd_ops[i].resolve_source, kCFRunLoopDefaultMode);
                CFRelease(m->dnssd_ops[i].resolve_source);
                m->dnssd_ops[i].resolve_source = NULL;
            }
            if (m->dnssd_ops[i].resolve_fdref) {
                CFFileDescriptorInvalidate(m->dnssd_ops[i].resolve_fdref);
                CFRelease(m->dnssd_ops[i].resolve_fdref);
                m->dnssd_ops[i].resolve_fdref = NULL;
            }
            if (m->dnssd_ops[i].resolve_ref) {
                DNSServiceRefDeallocate(m->dnssd_ops[i].resolve_ref);
                m->dnssd_ops[i].resolve_ref = NULL;
            }
            if (m->dnssd_ops[i].addr_source) {
                CFRunLoopRemoveSource(CFRunLoopGetCurrent(), m->dnssd_ops[i].addr_source, kCFRunLoopDefaultMode);
                CFRelease(m->dnssd_ops[i].addr_source);
                m->dnssd_ops[i].addr_source = NULL;
            }
            if (m->dnssd_ops[i].addr_fdref) {
                CFFileDescriptorInvalidate(m->dnssd_ops[i].addr_fdref);
                CFRelease(m->dnssd_ops[i].addr_fdref);
                m->dnssd_ops[i].addr_fdref = NULL;
            }
            if (m->dnssd_ops[i].addr_ref) {
                DNSServiceRefDeallocate(m->dnssd_ops[i].addr_ref);
                m->dnssd_ops[i].addr_ref = NULL;
            }
            m->dnssd_ops[i].in_use = 0;
        }

        if (m->dnssd_browse_source) {
            CFRunLoopRemoveSource(CFRunLoopGetCurrent(), m->dnssd_browse_source, kCFRunLoopDefaultMode);
            CFRelease(m->dnssd_browse_source);
            m->dnssd_browse_source = NULL;
        }
        if (m->dnssd_browse_fdref) {
            CFFileDescriptorInvalidate(m->dnssd_browse_fdref);
            CFRelease(m->dnssd_browse_fdref);
            m->dnssd_browse_fdref = NULL;
        }
        if (m->dnssd_browse_ref) {
            DNSServiceRefDeallocate(m->dnssd_browse_ref);
            m->dnssd_browse_ref = NULL;
        }
    }
#endif

    if (m->m) {
        platform_mdns_close(m->m);
        m->m = NULL;
    }
    if (m->on_event_fn) {
        RELEASE(m->on_event_fn);
        m->on_event_fn = NULL;
    }
    if (m->resolver_storage) {
        CLJ_FREE(m->resolver_storage);
        m->resolver_storage = NULL;
        m->resolver = NULL;
    }
    if (m->handle_bytes) {
        CLJ_FREE(m->handle_bytes);
        m->handle_bytes = NULL;
    }
    CLJ_FREE(m);
}

// Forward declaration: used by the macOS DNS-SD (mDNSResponder) backend helpers.
static void mdns_emit_event(void *ctx, MdnsEventType type, const MdnsResolvedService *svc);

#ifdef __APPLE__
static void dnssd_fd_cb(CFFileDescriptorRef fdref, CFOptionFlags callBackTypes, void *info) {
    if (!fdref || !info) return;
    if ((callBackTypes & kCFFileDescriptorReadCallBack) == 0) return;
    DNSServiceRef sdref = (DNSServiceRef)info;
    (void)DNSServiceProcessResult(sdref);
    CFFileDescriptorEnableCallBacks(fdref, kCFFileDescriptorReadCallBack);
}

static int dnssd_schedule(DNSServiceRef sdref, CFFileDescriptorRef *fdref_out, CFRunLoopSourceRef *source_out) {
    if (!sdref || !fdref_out || !source_out) return -1;
    int fd = DNSServiceRefSockFD(sdref);
    if (fd < 0) return -1;
    CFFileDescriptorContext ctx = {0};
    ctx.info = sdref;
    CFFileDescriptorRef fdref = CFFileDescriptorCreate(kCFAllocatorDefault, fd, true, dnssd_fd_cb, &ctx);
    if (!fdref) return -1;
    CFRunLoopSourceRef source = CFFileDescriptorCreateRunLoopSource(kCFAllocatorDefault, fdref, 0);
    if (!source) {
        CFFileDescriptorInvalidate(fdref);
        CFRelease(fdref);
        return -1;
    }
    CFFileDescriptorEnableCallBacks(fdref, kCFFileDescriptorReadCallBack);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode);
    *fdref_out = fdref;
    *source_out = source;
    return 0;
}

static void dnssd_unschedule(CFFileDescriptorRef *fdref_inout, CFRunLoopSourceRef *source_inout) {
    if (source_inout && *source_inout) {
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), *source_inout, kCFRunLoopDefaultMode);
        CFRelease(*source_inout);
        *source_inout = NULL;
    }
    if (fdref_inout && *fdref_inout) {
        CFFileDescriptorInvalidate(*fdref_inout);
        CFRelease(*fdref_inout);
        *fdref_inout = NULL;
    }
}

static void dnssd_trim_trailing_dot(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && s[n - 1] == '.') {
        s[n - 1] = '\0';
        n--;
    }
}

static int dnssd_endswith_local(const char *s) {
    if (!s) return 0;
    size_t n = strlen(s);
    if (n < 6) return 0;
    const char *p = s + (n - 6);
    if (p[0] != '.') return 0;
    return ((p[1] == 'l' || p[1] == 'L') &&
            (p[2] == 'o' || p[2] == 'O') &&
            (p[3] == 'c' || p[3] == 'C') &&
            (p[4] == 'a' || p[4] == 'A') &&
            (p[5] == 'l' || p[5] == 'L'));
}

static int dnssd_parse_browse_service(const char *service_fullname,
                                      char *regtype_out, size_t regtype_cap,
                                      char *domain_out, size_t domain_cap) {
    if (!service_fullname || !regtype_out || !domain_out) return -1;
    regtype_out[0] = '\0';
    domain_out[0] = '\0';

    // Normalize: tolerate trailing dots.
    char tmp[256];
    (void)mini_snprintf(tmp, sizeof(tmp), "%s", service_fullname);
    dnssd_trim_trailing_dot(tmp);

    // Default to "local." if no explicit domain is embedded.
    (void)mini_snprintf(domain_out, domain_cap, "local.");

    // If suffix is ".local" (case-insensitive), strip it and use "local." domain.
    if (dnssd_endswith_local(tmp)) {
        tmp[strlen(tmp) - 6] = '\0';
        dnssd_trim_trailing_dot(tmp);
    }

    if (tmp[0] == '\0') return -1;
    (void)mini_snprintf(regtype_out, regtype_cap, "%s", tmp);
    dnssd_trim_trailing_dot(regtype_out);
    return 0;
}

static int dnssd_is_services_meta_query(const char *browse_service) {
    if (!browse_service) return 0;
    return (strncmp(browse_service, "_services._dns-sd._udp", 21) == 0);
}

static void dnssd_cleanup_op(MdnsCtx *m, size_t idx) {
    if (!m) return;
    if (idx >= (sizeof(m->dnssd_ops) / sizeof(m->dnssd_ops[0]))) return;
    if (!m->dnssd_ops[idx].in_use) return;

    DnssdOp *op = &m->dnssd_ops[idx];
    dnssd_unschedule(&op->resolve_fdref, &op->resolve_source);
    dnssd_unschedule(&op->addr_fdref, &op->addr_source);
    if (op->resolve_ref) { DNSServiceRefDeallocate(op->resolve_ref); op->resolve_ref = NULL; }
    if (op->addr_ref) { DNSServiceRefDeallocate(op->addr_ref); op->addr_ref = NULL; }
    memset(op, 0, sizeof(*op));
}

static DnssdOp *dnssd_alloc_op(MdnsCtx *m) {
    if (!m) return NULL;
    for (size_t i = 0; i < (sizeof(m->dnssd_ops) / sizeof(m->dnssd_ops[0])); i++) {
        if (!m->dnssd_ops[i].in_use) {
            memset(&m->dnssd_ops[i], 0, sizeof(m->dnssd_ops[i]));
            m->dnssd_ops[i].in_use = 1;
            m->dnssd_ops[i].slot_index = i;
            m->dnssd_ops[i].owner = m;
            return &m->dnssd_ops[i];
        }
    }
    return NULL;
}

static void dnssd_finalize_op_if_ready(DnssdOp *op) {
    if (!op || !op->owner) return;
    if (!op->got_resolve) return;
    if (!op->got_addr_done) return;

    MdnsResolvedService svc;
    memset(&svc, 0, sizeof(svc));
    (void)mini_snprintf(svc.instance, sizeof(svc.instance), "%s", op->service_name);
    (void)mini_snprintf(svc.service, sizeof(svc.service), "%s", op->owner->dnssd_browse_service);
    (void)mini_snprintf(svc.host, sizeof(svc.host), "%s", op->host);
    svc.port = op->port;

    svc.txt_len = op->txt_len;
    if (svc.txt_len > sizeof(svc.txt)) svc.txt_len = sizeof(svc.txt);
    memcpy(svc.txt, op->txt, svc.txt_len);

    svc.addr_count = op->addr_count;
    if (svc.addr_count > 4) svc.addr_count = 4;
    for (size_t i = 0; i < svc.addr_count; i++) {
        (void)mini_snprintf(svc.addrs[i], sizeof(svc.addrs[i]), "%s", op->addrs[i]);
    }

    mdns_emit_event(op->owner, MDNS_EVENT_RESOLVED, &svc);
    dnssd_cleanup_op(op->owner, op->slot_index);
}

static void DNSSD_API dnssd_addr_cb(DNSServiceRef sdRef,
                                   DNSServiceFlags flags,
                                   uint32_t interfaceIndex,
                                   DNSServiceErrorType errorCode,
                                   const char *hostname,
                                   const struct sockaddr *address,
                                   uint32_t ttl,
                                   void *context) {
    (void)sdRef;
    (void)interfaceIndex;
    (void)hostname;
    (void)ttl;
    if (errorCode != kDNSServiceErr_NoError || !context || !address) return;
    DnssdOp *op = (DnssdOp*)context;

    if (op->addr_count < 4) {
        char buf[INET6_ADDRSTRLEN];
        buf[0] = '\0';
        if (address->sa_family == AF_INET) {
            const struct sockaddr_in *sin = (const struct sockaddr_in*)address;
            (void)inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
        } else if (address->sa_family == AF_INET6) {
            const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6*)address;
            (void)inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof(buf));
        }
        if (buf[0] != '\0') {
            (void)mini_snprintf(op->addrs[op->addr_count], sizeof(op->addrs[op->addr_count]), "%s", buf);
            op->addr_count++;
        }
    }

    if ((flags & kDNSServiceFlagsMoreComing) == 0) {
        op->got_addr_done = 1;
        dnssd_finalize_op_if_ready(op);
    }
}

static void DNSSD_API dnssd_resolve_cb(DNSServiceRef sdRef,
                                      DNSServiceFlags flags,
                                      uint32_t interfaceIndex,
                                      DNSServiceErrorType errorCode,
                                      const char *fullname,
                                      const char *hosttarget,
                                      uint16_t port,
                                      uint16_t txtLen,
                                      const unsigned char *txtRecord,
                                      void *context) {
    (void)sdRef;
    (void)flags;
    (void)fullname;
    if (!context) return;
    DnssdOp *op = (DnssdOp*)context;
    if (!op->owner) return;

    if (errorCode != kDNSServiceErr_NoError) {
        dnssd_cleanup_op(op->owner, op->slot_index);
        return;
    }

    (void)mini_snprintf(op->host, sizeof(op->host), "%s", hosttarget ? hosttarget : "");
    op->port = ntohs(port);

    op->txt_len = (size_t)txtLen;
    if (op->txt_len > sizeof(op->txt)) op->txt_len = sizeof(op->txt);
    if (txtRecord && op->txt_len > 0) {
        memcpy(op->txt, txtRecord, op->txt_len);
    }
    op->got_resolve = 1;

    if (!op->addr_ref && hosttarget && hosttarget[0] != '\0') {
        DNSServiceErrorType err = DNSServiceGetAddrInfo(&op->addr_ref,
                                                        0,
                                                        interfaceIndex,
                                                        kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6,
                                                        hosttarget,
                                                        dnssd_addr_cb,
                                                        op);
        if (err == kDNSServiceErr_NoError && op->addr_ref) {
            if (dnssd_schedule(op->addr_ref, &op->addr_fdref, &op->addr_source) != 0) {
                op->got_addr_done = 1;
            }
        } else {
            op->got_addr_done = 1;
        }
    } else {
        op->got_addr_done = 1;
    }

    dnssd_finalize_op_if_ready(op);
}

static void DNSSD_API tinyclj_dnssd_browse_cb(DNSServiceRef sdRef,
                                     DNSServiceFlags flags,
                                     uint32_t interfaceIndex,
                                     DNSServiceErrorType errorCode,
                                     const char *serviceName,
                                     const char *regtype,
                                     const char *replyDomain,
                                     void *context) {
    (void)sdRef;
    if (!context) return;
    MdnsCtx *m = (MdnsCtx*)context;
    if (errorCode != kDNSServiceErr_NoError) return;
    if (!serviceName || !regtype || !replyDomain) return;

    const int is_add = (flags & kDNSServiceFlagsAdd) ? 1 : 0;

    if (dnssd_is_services_meta_query(m->dnssd_browse_service)) {
        char full[256];
        (void)mini_snprintf(full, sizeof(full), "%s.%s%s", serviceName, regtype, replyDomain);
        dnssd_trim_trailing_dot(full);

        MdnsResolvedService svc;
        memset(&svc, 0, sizeof(svc));
        (void)mini_snprintf(svc.instance, sizeof(svc.instance), "%s", full);
        (void)mini_snprintf(svc.service, sizeof(svc.service), "%s", m->dnssd_browse_service);
        mdns_emit_event(m, is_add ? MDNS_EVENT_INSTANCE_FOUND : MDNS_EVENT_EXPIRED, &svc);
        return;
    }

    // Emit instance found/expired for the instance name.
    {
        MdnsResolvedService svc;
        memset(&svc, 0, sizeof(svc));
        (void)mini_snprintf(svc.instance, sizeof(svc.instance), "%s", serviceName);
        (void)mini_snprintf(svc.service, sizeof(svc.service), "%s", m->dnssd_browse_service);
        mdns_emit_event(m, is_add ? MDNS_EVENT_INSTANCE_FOUND : MDNS_EVENT_EXPIRED, &svc);
    }

    if (!is_add) return;

    DnssdOp *op = dnssd_alloc_op(m);
    if (!op) return;

    (void)mini_snprintf(op->service_name, sizeof(op->service_name), "%s", serviceName);
    (void)mini_snprintf(op->regtype, sizeof(op->regtype), "%s", regtype);
    (void)mini_snprintf(op->domain, sizeof(op->domain), "%s", replyDomain);

    DNSServiceErrorType err = DNSServiceResolve(&op->resolve_ref,
                                                0,
                                                interfaceIndex,
                                                serviceName,
                                                regtype,
                                                replyDomain,
                                                dnssd_resolve_cb,
                                                op);
    if (err != kDNSServiceErr_NoError || !op->resolve_ref) {
        dnssd_cleanup_op(m, op->slot_index);
        return;
    }
    if (dnssd_schedule(op->resolve_ref, &op->resolve_fdref, &op->resolve_source) != 0) {
        dnssd_cleanup_op(m, op->slot_index);
        return;
    }
}
#endif

static MdnsCtx* require_mdns_ctx(ID arg, const char *fn_name) {
    if (!arg || TAG(arg) != CLJ_BYTE_ARRAY) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "%s expects a mdns handle (byte-array)", fn_name);
        return NULL;
    }
    CljByteArray *ba = as_byte_array(arg);
    if (!ba || (ba->base.flags & CLJ_FLAG_BYTE_ARRAY_EXTERNAL) == 0) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "%s expects a native mdns handle (external byte-array)", fn_name);
        return NULL;
    }
    CljByteArrayExternal *ext = (CljByteArrayExternal*)ba;
    MdnsCtx *ctx = (MdnsCtx*)ext->external_ctx;
    if (!ctx) {
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                  "%s: handle is invalid (NULL ctx)", fn_name);
        return NULL;
    }
    return ctx;
}

static void mdns_emit_event(void *ctx, MdnsEventType type, const MdnsResolvedService *svc) {
    MdnsCtx *m = (MdnsCtx*)ctx;
    if (!m || !m->on_event_fn || !svc) return;

    // This function can be called from platform network callbacks (e.g. macOS CFRunLoop
    // socket callbacks). Those do not necessarily run with an active autorelease pool.
    // Many collection/string helpers use AUTORELEASE internally, so ensure a pool exists.
    AUTORELEASE_POOL_BEGIN();

    mdns_init_keywords();

    CljMap *ev = make_map(6);
    if (!ev) { AUTORELEASE_POOL_END(); return; }

    ID type_val = NULL;
    if (type == MDNS_EVENT_INSTANCE_FOUND) type_val = kw_instance_found;
    else if (type == MDNS_EVENT_RESOLVED) type_val = kw_resolved;
    else if (type == MDNS_EVENT_EXPIRED) type_val = kw_expired;

    ASSIGN(ev, map_assoc(ev, (ID)SYM_KW_TYPE, type_val));
    ASSIGN(ev, map_assoc(ev, kw_instance, (ID)make_string(svc->instance)));
    ASSIGN(ev, map_assoc(ev, kw_service, (ID)make_string(svc->service)));

    // Optional fields for resolved.
    if (type == MDNS_EVENT_RESOLVED) {
        ASSIGN(ev, map_assoc(ev, (ID)SYM_KW_HOST, (ID)make_string(svc->host)));
        ASSIGN(ev, map_assoc(ev, (ID)SYM_KW_PORT, fixnum((int32_t)svc->port)));

        // :addrs => vector of strings
        CljVector *addrs = make_vector((unsigned int)svc->addr_count, CLJ_VECTOR);
        if (addrs) {
            for (size_t i = 0; i < svc->addr_count; i++) {
                ID s = (ID)make_string(svc->addrs[i]);
                if (!s) continue;
                vector_conj_inplace(&addrs, s);
                RELEASE(s);
            }
            ASSIGN(ev, map_assoc(ev, kw_addrs, (ID)addrs));
            RELEASE((ID)addrs);
        }

        // :txt => byte-array (copy)
        CljByteArray *txt = make_byte_array((int)svc->txt_len);
        if (txt && svc->txt_len > 0) {
            memcpy(txt->data, svc->txt, svc->txt_len);
            ASSIGN(ev, map_assoc(ev, kw_txt, (ID)txt));
            RELEASE((ID)txt);
        }
    }

    EvalState *st = builtin_get_eval_state();
    if (!st) st = get_global_eval_state();
    ID args[1];
    args[0] = (ID)ev;

    TRY {
        (void)eval_function_call(m->on_event_fn, args, 1, NULL, st);
    } CATCH(ex) {
        (void)ex;
        // Swallow to avoid crashing platform callback.
    } END_TRY

    RELEASE((ID)ev);
    AUTORELEASE_POOL_END();
}

static void mdns_recv_bridge(void *ctx,
                             void *packet_handle,
                             const uint8_t *data,
                             size_t len,
                             const char *from_addr,
                             uint16_t from_port) {
    (void)from_addr;
    (void)from_port;
    MdnsCtx *m = (MdnsCtx*)ctx;
    if (!m || !m->resolver) {
        if (packet_handle) platform_net_packet_release(packet_handle);
        return;
    }
    (void)mdns_resolver_on_message(m->resolver, data, len);
    if (packet_handle) platform_net_packet_release(packet_handle);
}

ID native_tinyclj_net_mdns_open(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 0, "tinyclj.net.mdns/open");
    (void)args;

    MdnsCtx *m = (MdnsCtx*)CLJ_MALLOC(sizeof(MdnsCtx));
    if (!m) { throw_oom(); return NULL; }
    memset(m, 0, sizeof(*m));

#ifdef __APPLE__
    // Default on macOS: use mDNSResponder (DNS-SD) for correctness across interfaces.
    // Can be disabled for debugging via: TINYCLJ_MDNS_USE_DNSSD=0
    m->use_dnssd = 1;
    const char *use_dnssd = getenv("TINYCLJ_MDNS_USE_DNSSD");
    if (use_dnssd && use_dnssd[0] == '0') m->use_dnssd = 0;
#endif

    if (!m->use_dnssd) {
        // Allocate resolver storage.
        size_t need = mdns_resolver_storage_size();
        void *storage = CLJ_MALLOC(need);
        if (!storage) { mdns_ctx_free(m); throw_oom(); return NULL; }
        m->resolver_storage = storage;
        m->resolver = mdns_resolver_init(storage, need, mdns_emit_event, m);
        if (!m->resolver) { mdns_ctx_free(m); return NULL; }

        PlatformMdns *pm = platform_mdns_open(mdns_recv_bridge, m);
        if (!pm) {
            mdns_ctx_free(m);
            return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                             "tinyclj.net.mdns/open failed to open platform mDNS");
        }
        m->m = pm;
    }

    // Store ctx pointer bytes for debugging/inspection only.
    m->handle_bytes = (uint8_t*)CLJ_MALLOC(sizeof(ID));
    if (!m->handle_bytes) { mdns_ctx_free(m); throw_oom(); return NULL; }
    memcpy(m->handle_bytes, &m, sizeof(ID));

    CljByteArray *handle = make_byte_array_external(m->handle_bytes, (int)sizeof(ID), m, mdns_ctx_free);
    if (!handle) { mdns_ctx_free(m); return NULL; }
    return (ID)handle;
}

ID native_tinyclj_net_mdns_on_event(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "tinyclj.net.mdns/on-event");
    MdnsCtx *m = require_mdns_ctx(args[0], "tinyclj.net.mdns/on-event");
    if (!m) return NULL;

    ID fn = args[1];
    if (fn && !is_callable(fn)) {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net.mdns/on-event expects nil or a function");
    }
    if (m->on_event_fn) {
        RELEASE(m->on_event_fn);
        m->on_event_fn = NULL;
    }
    if (fn) m->on_event_fn = RETAIN(fn);
    return NULL;
}

ID native_tinyclj_net_mdns_browse_bang(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "tinyclj.net.mdns/browse!");
    MdnsCtx *m = require_mdns_ctx(args[0], "tinyclj.net.mdns/browse!");
    if (!m) return NULL;

    ID service_val = args[1];
    if (!service_val || TAG(service_val) != CLJ_STRING) {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net.mdns/browse! expects a service name string");
    }
    const char *service = string_data(service_val);
    if (!service || service[0] == '\0') {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net.mdns/browse! expects a non-empty service name string");
    }

#ifdef __APPLE__
    if (m->use_dnssd) {
        // Stop any previous browse.
        if (m->dnssd_browse_ref) {
            dnssd_unschedule(&m->dnssd_browse_fdref, &m->dnssd_browse_source);
            DNSServiceRefDeallocate(m->dnssd_browse_ref);
            m->dnssd_browse_ref = NULL;
        }
        // Clear outstanding ops as well.
        for (size_t i = 0; i < (sizeof(m->dnssd_ops) / sizeof(m->dnssd_ops[0])); i++) {
            dnssd_cleanup_op(m, i);
        }

        (void)mini_snprintf(m->dnssd_browse_service, sizeof(m->dnssd_browse_service), "%s", service);

        char regtype[256];
        char domain[256];
        if (dnssd_parse_browse_service(service, regtype, sizeof(regtype), domain, sizeof(domain)) != 0) {
            return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                             "tinyclj.net.mdns/browse! invalid service name");
        }

        DNSServiceErrorType err = DNSServiceBrowse(&m->dnssd_browse_ref,
                                                   0,
                                                   0,
                                                   regtype,
                                                   domain,
                                                   tinyclj_dnssd_browse_cb,
                                                   m);
        if (err != kDNSServiceErr_NoError || !m->dnssd_browse_ref) {
            m->dnssd_browse_ref = NULL;
            return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                             "tinyclj.net.mdns/browse! DNSServiceBrowse failed");
        }
        if (dnssd_schedule(m->dnssd_browse_ref, &m->dnssd_browse_fdref, &m->dnssd_browse_source) != 0) {
            DNSServiceRefDeallocate(m->dnssd_browse_ref);
            m->dnssd_browse_ref = NULL;
            return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                             "tinyclj.net.mdns/browse! failed to attach DNS-SD fd to CFRunLoop");
        }
        return NULL;
    }
#endif

    if (!m->m || !m->resolver) return NULL;

    if (mdns_resolver_start_browse(m->resolver, service) != 0) {
        return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                         "tinyclj.net.mdns/browse! failed to start browse");
    }

    uint8_t buf[512];
    size_t out_len = 0;
    int rc = mdns_build_ptr_query(service, buf, sizeof(buf), &out_len);
    if (rc != MDNS_CODEC_OK) {
        return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                         "tinyclj.net.mdns/browse! failed to build query");
    }

    if (platform_mdns_send_multicast(m->m, buf, out_len) != 0) {
        return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                         "tinyclj.net.mdns/browse! failed to send mDNS query");
    }
    return NULL;
}

ID native_tinyclj_net_mdns_close_bang(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "tinyclj.net.mdns/close!");
    MdnsCtx *m = require_mdns_ctx(args[0], "tinyclj.net.mdns/close!");
    if (!m) return NULL;

    // Best-effort close now; finalizer will also close if user drops the handle.
    if (m->m) {
        platform_mdns_close(m->m);
        m->m = NULL;
    }
    return NULL;
}

