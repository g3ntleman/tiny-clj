/**
 * builtins_net.c - Native bindings for tinyclj.net
 *
 * Channel/callback style networking primitives (UDP first).
 * Designed for low RAM overhead and zero-copy receive buffers:
 * - macOS: packet_handle is a retained CFDataRef
 * - ESP32: packet_handle is a pbuf* with refcount held until released
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
#include "to_string.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct NetPacketCtx {
    void *packet_handle;
} NetPacketCtx;

static void net_packet_release_cb(void *ctx) {
    NetPacketCtx *p = (NetPacketCtx*)ctx;
    if (!p) return;
    if (p->packet_handle) {
        platform_net_packet_release(p->packet_handle);
        p->packet_handle = NULL;
    }
    CLJ_FREE(p);
}

#define NET_TYPE_UDP 0
#define NET_TYPE_TCP 1

typedef struct NetCtx {
    union {
        PlatformUdpSocket *udp_sock;
        PlatformTcpConn *tcp_conn;
    } handle;
    ID on_receive_fn;
    uint8_t *handle_bytes;
    uint8_t type;
} NetCtx;

static void net_ctx_free(void *ctx) {
    NetCtx *c = (NetCtx*)ctx;
    if (!c) return;
    if (c->type == NET_TYPE_UDP && c->handle.udp_sock) {
        platform_udp_close(c->handle.udp_sock);
        c->handle.udp_sock = NULL;
    } else if (c->type == NET_TYPE_TCP && c->handle.tcp_conn) {
        platform_tcp_close(c->handle.tcp_conn);
        c->handle.tcp_conn = NULL;
    }
    if (c->on_receive_fn) {
        RELEASE(c->on_receive_fn);
        c->on_receive_fn = NULL;
    }
    if (c->handle_bytes) {
        CLJ_FREE(c->handle_bytes);
        c->handle_bytes = NULL;
    }
    CLJ_FREE(c);
}

static NetCtx* require_net_ctx(ID arg, uint8_t expected_type, const char *fn_name) {
    const char *type_name = expected_type == NET_TYPE_UDP ? "udp socket" : "tcp connection";
    if (!arg || TAG(arg) != CLJ_BYTE_ARRAY) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "%s expects a %s handle (byte-array)", fn_name, type_name); return NULL;
        return NULL;
    }
    CljByteArray *ba = as_byte_array(arg);
    if (!ba || (ba->base.flags & CLJ_FLAG_BYTE_ARRAY_EXTERNAL) == 0) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "%s expects a native %s handle (external byte-array)", fn_name, type_name); return NULL;
        return NULL;
    }
    CljByteArrayExternal *ext = (CljByteArrayExternal*)ba;
    NetCtx *ctx = (NetCtx*)ext->external_ctx;
    if (!ctx) {
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                  "%s: handle is invalid (NULL ctx)", fn_name); return NULL;
        return NULL;
    }
    if (ctx->type != expected_type) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "%s expects a %s handle", fn_name, type_name); return NULL;
        return NULL;
    }
    return ctx;
}

static void net_udp_recv_bridge(void *ctx,
                                void *packet_handle,
                                const uint8_t *data,
                                size_t len,
                                const char *from_addr,
                                uint16_t from_port) {
    NetCtx *u = (NetCtx*)ctx;
    if (!u || !packet_handle) {
        if (packet_handle) platform_net_packet_release(packet_handle);
        return;
    }
    if (!u->on_receive_fn) {
        platform_net_packet_release(packet_handle);
        return;
    }

    // Wrap packet bytes as a zero-copy external byte-array. Its finalizer releases packet_handle.
    if (len > (size_t)INT32_MAX) {
        platform_net_packet_release(packet_handle);
        return;
    }
    NetPacketCtx *pc = (NetPacketCtx*)CLJ_MALLOC(sizeof(NetPacketCtx));
    if (!pc) {
        platform_net_packet_release(packet_handle);
        return;
    }
    pc->packet_handle = packet_handle;
    CljByteArray *payload = make_byte_array_external((uint8_t*)data, (int)len, pc, net_packet_release_cb);
    if (!payload) {
        net_packet_release_cb(pc);
        return;
    }

    CljMap *m = make_map(3);
    if (!m) {
        RELEASE((ID)payload);
        return;
    }

    // Build message map: {:data <byte-array> :from "ip" :port N}
    ASSIGN(m, map_by_associng_kv(m, (ID)SYM_KW_DATA, (ID)payload));
    ASSIGN(m, map_by_associng_kv(m, (ID)SYM_KW_FROM, (ID)make_string(from_addr ? from_addr : "")));
    ASSIGN(m, map_by_associng_kv(m, (ID)SYM_KW_PORT, fixnum((int32_t)from_port)));

    // Invoke user callback with the message.
    EvalState *st = builtin_get_eval_state();
    if (!st) st = get_global_eval_state();

    ID args[1];
    args[0] = (ID)m;

    TRY {
        (void)eval_function_call(u->on_receive_fn, args, 1, NULL, st);
    } CATCH(ex) {
        (void)ex;
        // Swallow exceptions so platform callback does not crash the host loop.
    } END_TRY

    RELEASE((ID)m);
}

ID native_tinyclj_net_udp_socket(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "tinyclj.net/udp-socket");
    if (!args[0] || TAG(args[0]) != CLJ_MAP) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/udp-socket expects an options map {:port N}"); return NULL;
    }
    CljMap *opts = (CljMap*)args[0];
    ID port_val = map_get_sentinel(opts, (ID)SYM_KW_PORT, NOT_FOUND);
    if (port_val == NOT_FOUND || !is_fixnum(port_val)) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/udp-socket expects :port fixnum"); return NULL;
    }
    int port_i = as_fixnum(port_val);
    if (port_i <= 0 || port_i > 65535) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/udp-socket :port out of range: %d", port_i); return NULL;
    }

    NetCtx *c = (NetCtx*)CLJ_MALLOC(sizeof(NetCtx));
    if (!c) {
        throw_oom();
        return NULL;
    }
    memset(c, 0, sizeof(*c));
    c->type = NET_TYPE_UDP;

    PlatformUdpSocket *sock = platform_udp_bind((uint16_t)port_i, net_udp_recv_bridge, c);
    if (!sock) {
        CLJ_FREE(c);
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                         "tinyclj.net/udp-socket failed to bind port %d", port_i); return NULL;
    }
    c->handle.udp_sock = sock;
    c->on_receive_fn = NULL;

    c->handle_bytes = (uint8_t*)CLJ_MALLOC(sizeof(ID));
    if (!c->handle_bytes) {
        platform_udp_close(sock);
        CLJ_FREE(c);
        throw_oom();
        return NULL;
    }
    memcpy(c->handle_bytes, &c, sizeof(ID));

    CljByteArray *handle = make_byte_array_external(c->handle_bytes, (int)sizeof(ID), c, net_ctx_free);
    if (!handle) {
        net_ctx_free(c);
        return NULL;
    }
    return handle;
}

ID native_tinyclj_net_on_receive(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "tinyclj.net/on-receive");
    NetCtx *u = require_net_ctx(args[0], NET_TYPE_UDP, "tinyclj.net/on-receive");
    if (!u) return NULL;

    ID fn = args[1];
    if (fn && !is_callable(fn)) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/on-receive expects nil or a function"); return NULL;
    }

    if (u->on_receive_fn) {
        RELEASE(u->on_receive_fn);
        u->on_receive_fn = NULL;
    }
    if (fn) {
        u->on_receive_fn = RETAIN(fn);
    }
    return NULL;
}

ID native_tinyclj_net_send_bang(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "tinyclj.net/send!");
    NetCtx *c = require_net_ctx(args[0], NET_TYPE_UDP, "tinyclj.net/send!");
    if (!c || !c->handle.udp_sock) return NULL;

    if (!args[1] || TAG(args[1]) != CLJ_MAP) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/send! expects a map {:to \"ip\" :port N :data byte-array}"); return NULL;
    }
    CljMap *m = (CljMap*)args[1];

    ID to_val = map_get_sentinel(m, (ID)SYM_KW_TO, NOT_FOUND);
    ID port_val = map_get_sentinel(m, (ID)SYM_KW_PORT, NOT_FOUND);
    ID data_val = map_get_sentinel(m, (ID)SYM_KW_DATA, NOT_FOUND);

    if (to_val == NOT_FOUND || !to_val) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/send! requires :to"); return NULL;
    }
    const char *to = string_data(to_string(to_val));
    if (!to) return NULL;

    if (port_val == NOT_FOUND || !is_fixnum(port_val)) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/send! requires :port fixnum"); return NULL;
    }
    int port_i = as_fixnum(port_val);
    if (port_i <= 0 || port_i > 65535) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/send! :port out of range: %d", port_i); return NULL;
    }

    if (data_val == NOT_FOUND || !data_val || TAG(data_val) != CLJ_BYTE_ARRAY) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/send! requires :data byte-array"); return NULL;
    }
    CljByteArray *ba = as_byte_array(data_val);

    int rc = platform_udp_send(c->handle.udp_sock, (const uint8_t*)ba->data, (size_t)ba->length, to, (uint16_t)port_i);
    if (rc != 0) {
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                         "tinyclj.net/send! failed"); return NULL;
    }
    return NULL;
}

ID native_tinyclj_net_close_bang(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "tinyclj.net/close!");
    NetCtx *c = require_net_ctx(args[0], NET_TYPE_UDP, "tinyclj.net/close!");
    if (!c) return NULL;
    if (c->handle.udp_sock) {
        platform_udp_close(c->handle.udp_sock);
        c->handle.udp_sock = NULL;
    }
    if (c->on_receive_fn) {
        RELEASE(c->on_receive_fn);
        c->on_receive_fn = NULL;
    }
    return NULL;
}

// -----------------------------------------------------------------------------
// TCP
// -----------------------------------------------------------------------------

static void net_tcp_event_bridge(void *ctx,
                                 PlatformTcpEvent event,
                                 void *packet_handle,
                                 const uint8_t *data,
                                 size_t len) {
    NetCtx *c = (NetCtx*)ctx;
    if (!c) {
        if (packet_handle) platform_net_packet_release(packet_handle);
        return;
    }

    // For now, only deliver DATA events to the callback.
    if (event != PLATFORM_TCP_EVENT_DATA) {
        if (packet_handle) platform_net_packet_release(packet_handle);
        return;
    }
    if (!c->on_receive_fn) {
        if (packet_handle) platform_net_packet_release(packet_handle);
        return;
    }
    if (!packet_handle || !data) return;
    if (len > (size_t)INT32_MAX) {
        platform_net_packet_release(packet_handle);
        return;
    }

    NetPacketCtx *pc = (NetPacketCtx*)CLJ_MALLOC(sizeof(NetPacketCtx));
    if (!pc) {
        platform_net_packet_release(packet_handle);
        return;
    }
    pc->packet_handle = packet_handle;
    CljByteArray *payload = make_byte_array_external((uint8_t*)data, (int)len, pc, net_packet_release_cb);
    if (!payload) {
        net_packet_release_cb(pc);
        return;
    }

    CljMap *m = make_map(1);
    if (!m) {
        RELEASE((ID)payload);
        return;
    }
    ASSIGN(m, map_by_associng_kv(m, (ID)SYM_KW_DATA, (ID)payload));

    EvalState *st = builtin_get_eval_state();
    if (!st) st = get_global_eval_state();

    ID args[1];
    args[0] = (ID)m;

    TRY {
        (void)eval_function_call(c->on_receive_fn, args, 1, NULL, st);
    } CATCH(ex) {
        (void)ex;
    } END_TRY

    RELEASE((ID)m);
}

ID native_tinyclj_net_tcp_connect(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "tinyclj.net/tcp-connect");
    if (!args[0] || TAG(args[0]) != CLJ_MAP) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/tcp-connect expects {:host \"...\" :port N}"); return NULL;
    }
    CljMap *opts = (CljMap*)args[0];
    ID host_val = map_get_sentinel(opts, (ID)SYM_KW_HOST, NOT_FOUND);
    ID port_val = map_get_sentinel(opts, (ID)SYM_KW_PORT, NOT_FOUND);
    if (host_val == NOT_FOUND || !host_val) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/tcp-connect requires :host"); return NULL;
    }
    const char *host = string_data(to_string(host_val));
    if (!host) return NULL;
    if (port_val == NOT_FOUND || !is_fixnum(port_val)) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/tcp-connect requires :port fixnum"); return NULL;
    }
    int port_i = as_fixnum(port_val);
    if (port_i <= 0 || port_i > 65535) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/tcp-connect :port out of range: %d", port_i); return NULL;
    }

    NetCtx *c = (NetCtx*)CLJ_MALLOC(sizeof(NetCtx));
    if (!c) {
        throw_oom();
        return NULL;
    }
    memset(c, 0, sizeof(*c));
    c->type = NET_TYPE_TCP;

    PlatformTcpConn *conn = platform_tcp_connect_async(host, (uint16_t)port_i, net_tcp_event_bridge, c);
    if (!conn) {
        CLJ_FREE(c);
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                         "tinyclj.net/tcp-connect failed"); return NULL;
    }
    c->handle.tcp_conn = conn;
    c->on_receive_fn = NULL;

    c->handle_bytes = (uint8_t*)CLJ_MALLOC(sizeof(ID));
    if (!c->handle_bytes) {
        platform_tcp_close(conn);
        CLJ_FREE(c);
        throw_oom();
        return NULL;
    }
    memcpy(c->handle_bytes, &c, sizeof(ID));

    CljByteArray *handle = make_byte_array_external(c->handle_bytes, (int)sizeof(ID), c, net_ctx_free);
    if (!handle) {
        net_ctx_free(c);
        return NULL;
    }
    return handle;
}

ID native_tinyclj_net_tcp_on_receive(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "tinyclj.net/tcp-on-receive");
    NetCtx *c = require_net_ctx(args[0], NET_TYPE_TCP, "tinyclj.net/tcp-on-receive");
    if (!c) return NULL;
    ID fn = args[1];
    if (fn && !is_callable(fn)) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/tcp-on-receive expects nil or a function"); return NULL;
    }
    if (c->on_receive_fn) {
        RELEASE(c->on_receive_fn);
        c->on_receive_fn = NULL;
    }
    if (fn) c->on_receive_fn = RETAIN(fn);
    return NULL;
}

ID native_tinyclj_net_tcp_send_bang(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "tinyclj.net/tcp-send!");
    NetCtx *c = require_net_ctx(args[0], NET_TYPE_TCP, "tinyclj.net/tcp-send!");
    if (!c || !c->handle.tcp_conn) return NULL;

    if (!args[1] || TAG(args[1]) != CLJ_MAP) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/tcp-send! expects {:data byte-array}"); return NULL;
    }
    CljMap *m = (CljMap*)args[1];
    ID data_val = map_get_sentinel(m, (ID)SYM_KW_DATA, NOT_FOUND);
    if (data_val == NOT_FOUND || !data_val || TAG(data_val) != CLJ_BYTE_ARRAY) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/tcp-send! requires :data byte-array"); return NULL;
    }
    CljByteArray *ba = as_byte_array(data_val);
    int rc = platform_tcp_send(c->handle.tcp_conn, (const uint8_t*)ba->data, (size_t)ba->length);
    if (rc != 0) {
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                         "tinyclj.net/tcp-send! failed"); return NULL;
    }
    return NULL;
}

ID native_tinyclj_net_tcp_close_bang(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "tinyclj.net/tcp-close!");
    NetCtx *c = require_net_ctx(args[0], NET_TYPE_TCP, "tinyclj.net/tcp-close!");
    if (!c) return NULL;
    if (c->handle.tcp_conn) {
        platform_tcp_close(c->handle.tcp_conn);
        c->handle.tcp_conn = NULL;
    }
    if (c->on_receive_fn) {
        RELEASE(c->on_receive_fn);
        c->on_receive_fn = NULL;
    }
    return NULL;
}
