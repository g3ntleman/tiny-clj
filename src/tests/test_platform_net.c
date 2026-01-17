/*
 * Unity Platform Network Tests (macOS)
 *
 * Verifies the platform UDP API works on host builds and integrates with CFRunLoop.
 */

#include "tests_common.h"
#include "../platform.h"

typedef struct {
    bool got;
    uint8_t data[32];
    size_t len;
    void *packet_handle;
} UdpRecvCtx;

static void udp_test_recv(void *ctx,
                          void *packet_handle,
                          const uint8_t *data,
                          size_t len,
                          const char *from_addr,
                          uint16_t from_port) {
    (void)from_addr;
    (void)from_port;
    UdpRecvCtx *r = (UdpRecvCtx*)ctx;
    if (!r || !data) return;
    r->got = true;
    r->packet_handle = packet_handle;
    if (len > sizeof(r->data)) len = sizeof(r->data);
    memcpy(r->data, data, len);
    r->len = len;
}

TEST(test_platform_udp_bind_send_loopback)
{
#if defined(__APPLE__) && !defined(ESP32_BUILD)
    UdpRecvCtx r = {0};

    PlatformUdpSocket *sock = NULL;
    uint16_t port = 0;
    for (uint16_t p = 45000; p < 45100; p++) {
        sock = platform_udp_bind(p, udp_test_recv, &r);
        if (sock) { port = p; break; }
    }
    if (!sock) {
        // In some environments (e.g., sandboxed CI runners), socket operations can be denied (EPERM).
        TEST_IGNORE_MESSAGE("UDP bind failed (likely sandboxed environment without network syscalls)");
    }
    TEST_ASSERT_TRUE(port != 0);

    const uint8_t msg[] = {0xDE, 0xAD, 0xBE, 0xEF};
    TEST_ASSERT_EQUAL_INT(0, platform_udp_send(sock, msg, sizeof(msg), "127.0.0.1", port));

    // Run the platform runloop to allow CFSocket callbacks to fire.
    for (int i = 0; i < 200 && !r.got; i++) {
        platform_runloop_run_once(1);
    }

    TEST_ASSERT_TRUE_MESSAGE(r.got, "Did not receive UDP loopback packet");
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(msg), (uint32_t)r.len);
    TEST_ASSERT_EQUAL_MEMORY(msg, r.data, sizeof(msg));

    // Release the packet handle (CFDataRef on macOS).
    platform_net_packet_release(r.packet_handle);

    platform_udp_close(sock);
#else
    TEST_IGNORE_MESSAGE("Platform UDP tests only run on macOS host builds");
#endif
}

