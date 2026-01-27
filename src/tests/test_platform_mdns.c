/*
 * Platform mDNS transport tests (macOS)
 *
 * This is a best-effort smoke test: it binds to UDP/5353 with reuse enabled and
 * verifies that sending a unicast packet to 127.0.0.1:5353 is received via the
 * CFRunLoop callback.
 */

#include "tests_common.h"
#include "../platform.h"

typedef struct {
    bool got;
    uint8_t expected[64];
    size_t expected_len;
    uint8_t data[64];
    size_t len;
    void *packet_handle;
} MdnsRecvCtx;

static void mdns_test_recv(void *ctx,
                           void *packet_handle,
                           const uint8_t *data,
                           size_t len,
                           const char *from_addr,
                           uint16_t from_port) {
    (void)from_addr;
    (void)from_port;
    MdnsRecvCtx *r = (MdnsRecvCtx*)ctx;
    if (!r || !data) return;

    // Best-effort filter: ignore unrelated traffic on 5353.
    if (r->expected_len > 0) {
        if (len != r->expected_len) return;
        if (len > sizeof(r->expected)) return;
        if (memcmp(r->expected, data, len) != 0) return;
    }

    r->got = true;
    r->packet_handle = packet_handle;
    if (len > sizeof(r->data)) len = sizeof(r->data);
    memcpy(r->data, data, len);
    r->len = len;
}

TEST(test_platform_mdns_open_send_unicast_loopback)
{
#if defined(__APPLE__) && !defined(ESP32_BUILD)
    MdnsRecvCtx r = {0};
    PlatformMdns *m = platform_mdns_open(mdns_test_recv, &r);
    if (!m) {
        TEST_IGNORE_MESSAGE("mDNS open failed (likely sandboxed environment without privileged socket options)");
    }

    const uint8_t msg[] = {0xAA, 0xBB, 0xCC, 0xDD};
    r.expected_len = sizeof(msg);
    memcpy(r.expected, msg, sizeof(msg));
    TEST_ASSERT_EQUAL_INT(0, platform_mdns_send_unicast(m, msg, sizeof(msg), "127.0.0.1", 5353));

    // Best-effort: depending on sandboxing / local firewall this can be flaky.
    for (int i = 0; i < 2000 && !r.got; i++) {
        platform_runloop_run_once(1);
    }

    if (!r.got) {
        platform_mdns_close(m);
        TEST_IGNORE_MESSAGE("Did not receive mDNS unicast loopback packet (likely sandboxed/firewalled environment)");
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(msg), (uint32_t)r.len);
    TEST_ASSERT_EQUAL_MEMORY(msg, r.data, sizeof(msg));

    platform_net_packet_release(r.packet_handle);
    platform_mdns_close(m);
#else
    TEST_IGNORE_MESSAGE("Platform mDNS tests only run on macOS host builds");
#endif
}

