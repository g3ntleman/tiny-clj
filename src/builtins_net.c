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

typedef struct NetUdpCtx {
    PlatformUdpSocket *sock;
    ID on_receive_fn;    // retained, may be NULL
    uint8_t *handle_bytes;
} NetUdpCtx;

static void net_udp_ctx_free(void *ctx) {
    NetUdpCtx *u = (NetUdpCtx*)ctx;
    if (!u) return;
    if (u->sock) {
        platform_udp_close(u->sock);
        u->sock = NULL;
    }
    if (u->on_receive_fn) {
        RELEASE(u->on_receive_fn);
        u->on_receive_fn = NULL;
    }
    if (u->handle_bytes) {
        CLJ_FREE(u->handle_bytes);
        u->handle_bytes = NULL;
    }
    CLJ_FREE(u);
}

static NetUdpCtx* require_udp_ctx(ID arg, const char *fn_name) {
    if (!arg || TAG(arg) != CLJ_BYTE_ARRAY) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "%s expects a udp socket handle (byte-array)", fn_name);
        return NULL;
    }
    CljByteArray *ba = as_byte_array(arg);
    if (!ba || (ba->base.flags & CLJ_FLAG_BYTE_ARRAY_EXTERNAL) == 0) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "%s expects a native udp socket handle (external byte-array)", fn_name);
        return NULL;
    }
    CljByteArrayExternal *ext = (CljByteArrayExternal*)ba;
    NetUdpCtx *ctx = (NetUdpCtx*)ext->external_ctx;
    if (!ctx) {
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                  "%s: socket handle is invalid (NULL ctx)", fn_name);
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
    NetUdpCtx *u = (NetUdpCtx*)ctx;
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
    ASSIGN(m, map_assoc(m, (ID)SYM_KW_DATA, (ID)payload));
    ASSIGN(m, map_assoc(m, (ID)SYM_KW_FROM, (ID)make_string(from_addr ? from_addr : "")));
    ASSIGN(m, map_assoc(m, (ID)SYM_KW_PORT, fixnum((int32_t)from_port)));

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
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/udp-socket expects an options map {:port N}");
    }
    CljMap *opts = (CljMap*)args[0];
    ID port_val = map_get_sentinel(opts, (ID)SYM_KW_PORT, NOT_FOUND);
    if (port_val == NOT_FOUND || !is_fixnum(port_val)) {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/udp-socket expects :port fixnum");
    }
    int port_i = as_fixnum(port_val);
    if (port_i <= 0 || port_i > 65535) {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/udp-socket :port out of range: %d", port_i);
    }

    NetUdpCtx *u = (NetUdpCtx*)CLJ_MALLOC(sizeof(NetUdpCtx));
    if (!u) {
        throw_oom();
        return NULL;
    }
    memset(u, 0, sizeof(*u));

    PlatformUdpSocket *sock = platform_udp_bind((uint16_t)port_i, net_udp_recv_bridge, u);
    if (!sock) {
        CLJ_FREE(u);
        return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                         "tinyclj.net/udp-socket failed to bind port %d", port_i);
    }
    u->sock = sock;
    u->on_receive_fn = NULL;

    // Store the ctx pointer as bytes (debug/inspection only).
    u->handle_bytes = (uint8_t*)CLJ_MALLOC(sizeof(ID));
    if (!u->handle_bytes) {
        platform_udp_close(sock);
        CLJ_FREE(u);
        throw_oom();
        return NULL;
    }
    memcpy(u->handle_bytes, &u, sizeof(ID));

    // External byte-array whose finalizer closes the socket and frees ctx + bytes.
    CljByteArray *handle = make_byte_array_external(u->handle_bytes, (int)sizeof(ID), u, net_udp_ctx_free);
    if (!handle) {
        net_udp_ctx_free(u);
        return NULL;
    }
    return (ID)handle;
}

ID native_tinyclj_net_on_receive(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "tinyclj.net/on-receive");
    NetUdpCtx *u = require_udp_ctx(args[0], "tinyclj.net/on-receive");
    if (!u) return NULL;

    ID fn = args[1];
    if (fn && !is_callable(fn)) {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/on-receive expects nil or a function");
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
    NetUdpCtx *u = require_udp_ctx(args[0], "tinyclj.net/send!");
    if (!u || !u->sock) return NULL;

    if (!args[1] || TAG(args[1]) != CLJ_MAP) {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/send! expects a map {:to \"ip\" :port N :data byte-array}");
    }
    CljMap *m = (CljMap*)args[1];

    ID to_val = map_get_sentinel(m, (ID)SYM_KW_TO, NOT_FOUND);
    ID port_val = map_get_sentinel(m, (ID)SYM_KW_PORT, NOT_FOUND);
    ID data_val = map_get_sentinel(m, (ID)SYM_KW_DATA, NOT_FOUND);

    if (to_val == NOT_FOUND || !to_val) {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/send! requires :to");
    }
    const char *to = string_data(to_string(to_val));
    if (!to) return NULL;

    if (port_val == NOT_FOUND || !is_fixnum(port_val)) {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/send! requires :port fixnum");
    }
    int port_i = as_fixnum(port_val);
    if (port_i <= 0 || port_i > 65535) {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/send! :port out of range: %d", port_i);
    }

    if (data_val == NOT_FOUND || !data_val || TAG(data_val) != CLJ_BYTE_ARRAY) {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.net/send! requires :data byte-array");
    }
    CljByteArray *ba = as_byte_array(data_val);

    int rc = platform_udp_send(u->sock, (const uint8_t*)ba->data, (size_t)ba->length, to, (uint16_t)port_i);
    if (rc != 0) {
        return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                         "tinyclj.net/send! failed");
    }
    return NULL;
}

ID native_tinyclj_net_close_bang(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "tinyclj.net/close!");
    NetUdpCtx *u = require_udp_ctx(args[0], "tinyclj.net/close!");
    if (!u) return NULL;
    if (u->sock) {
        platform_udp_close(u->sock);
        u->sock = NULL;
    }
    if (u->on_receive_fn) {
        RELEASE(u->on_receive_fn);
        u->on_receive_fn = NULL;
    }
    return NULL;
}

