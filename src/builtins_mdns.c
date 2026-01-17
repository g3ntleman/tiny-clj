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

typedef struct MdnsCtx {
    PlatformMdns *m;
    MdnsResolver *resolver;
    void *resolver_storage;
    ID on_event_fn; // retained, may be NULL
    uint8_t *handle_bytes;
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

    mdns_init_keywords();

    CljMap *ev = make_map(6);
    if (!ev) return;

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
    if (!m || !m->m || !m->resolver) return NULL;

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

