/*
 * mDNS resolver tests (synthetic packets)
 */

#include "tests_common.h"
#include "../mdns_resolver.h"

typedef struct {
    int found_count;
    char last_instance[256];
    int resolved_count;
    uint16_t last_port;
    char last_host[256];
    char last_addr0[64];
    int expired_count;
} ResolverSpy;

static void spy_cb(void *ctx, MdnsEventType type, const MdnsResolvedService *svc) {
    (void)svc;
    ResolverSpy *s = (ResolverSpy *)ctx;
    if (!s) return;
    if (type == MDNS_EVENT_INSTANCE_FOUND) {
        s->found_count++;
        if (svc) {
            (void)mini_snprintf(s->last_instance, sizeof(s->last_instance), "%s", svc->instance);
        }
    } else if (type == MDNS_EVENT_RESOLVED) {
        s->resolved_count++;
        if (svc) {
            s->last_port = svc->port;
            (void)mini_snprintf(s->last_host, sizeof(s->last_host), "%s", svc->host);
            if (svc->addr_count > 0) {
                (void)mini_snprintf(s->last_addr0, sizeof(s->last_addr0), "%s", svc->addrs[0]);
            }
        }
    } else if (type == MDNS_EVENT_EXPIRED) {
        s->expired_count++;
    }
}

TEST(test_mdns_resolver_starts_browse_without_events)
{
    uint8_t storage[16384];
    ResolverSpy spy = {0};
    MdnsResolver *r = mdns_resolver_init(storage, sizeof(storage), spy_cb, &spy);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(0, mdns_resolver_start_browse(r, "_matterc._udp.local"));

    // No packets yet -> no events.
    TEST_ASSERT_EQUAL_INT(0, spy.found_count);
}

TEST(test_mdns_resolver_emits_instance_found_on_ptr_answer)
{
    uint8_t storage[16384];
    ResolverSpy spy = {0};
    MdnsResolver *r = mdns_resolver_init(storage, sizeof(storage), spy_cb, &spy);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(0, mdns_resolver_start_browse(r, "_matterc._udp.local"));

    // Minimal DNS response with one PTR answer:
    // NAME = _matterc._udp.local
    // RDATA = Tiny._matterc._udp.local
    const uint8_t msg[] = {
        0x00, 0x00, // id
        0x84, 0x00, // flags: response + authoritative
        0x00, 0x00, // qdcount
        0x00, 0x01, // ancount
        0x00, 0x00, // nscount
        0x00, 0x00, // arcount

        // NAME: _matterc._udp.local
        0x08, '_','m','a','t','t','e','r','c',
        0x04, '_','u','d','p',
        0x05, 'l','o','c','a','l',
        0x00,

        // TYPE=PTR, CLASS=IN
        0x00, 0x0C,
        0x00, 0x01,
        // TTL=120
        0x00, 0x00, 0x00, 0x78,
        // RDLENGTH (computed below: 1+4 + 1+8 + 1+4 + 1+5 + 1 = 26)
        0x00, 0x1A,

        // RDATA: Tiny._matterc._udp.local
        0x04, 'T','i','n','y',
        0x08, '_','m','a','t','t','e','r','c',
        0x04, '_','u','d','p',
        0x05, 'l','o','c','a','l',
        0x00,
    };

    TEST_ASSERT_EQUAL_INT(0, mdns_resolver_on_message(r, msg, sizeof(msg)));
    TEST_ASSERT_EQUAL_INT(1, spy.found_count);
    TEST_ASSERT_EQUAL_STRING("Tiny._matterc._udp.local", spy.last_instance);
}

TEST(test_mdns_resolver_emits_resolved_when_ptr_srv_txt_aaaa_present)
{
    uint8_t storage[16384];
    ResolverSpy spy = {0};
    MdnsResolver *r = mdns_resolver_init(storage, sizeof(storage), spy_cb, &spy);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(0, mdns_resolver_start_browse(r, "_matterc._udp.local"));
    TEST_ASSERT_EQUAL_INT(0, mdns_resolver_tick(r, 1000));

    // One response with:
    // - Answer: PTR _matterc._udp.local -> Tiny._matterc._udp.local
    // - Additional: SRV/TXT for instance + AAAA for host tiny.local
    const uint8_t msg[] = {
        0x00, 0x00, // id
        0x84, 0x00, // flags
        0x00, 0x00, // qdcount
        0x00, 0x01, // ancount
        0x00, 0x00, // nscount
        0x00, 0x03, // arcount

        // ANSWER: NAME _matterc._udp.local
        0x08, '_','m','a','t','t','e','r','c',
        0x04, '_','u','d','p',
        0x05, 'l','o','c','a','l',
        0x00,
        // TYPE PTR, CLASS IN, TTL 120, RDLEN 26
        0x00, 0x0C,
        0x00, 0x01,
        0x00, 0x00, 0x00, 0x78,
        0x00, 0x1A,
        // RDATA: Tiny._matterc._udp.local
        0x04, 'T','i','n','y',
        0x08, '_','m','a','t','t','e','r','c',
        0x04, '_','u','d','p',
        0x05, 'l','o','c','a','l',
        0x00,

        // ADDITIONAL 1: SRV for instance: Tiny._matterc._udp.local
        0x04, 'T','i','n','y',
        0x08, '_','m','a','t','t','e','r','c',
        0x04, '_','u','d','p',
        0x05, 'l','o','c','a','l',
        0x00,
        0x00, 0x21, // SRV
        0x00, 0x01, // IN
        0x00, 0x00, 0x00, 0x78,
        // RDLEN: 6 + len("tiny.local") = 6 + (1+4 + 1+5 + 1) = 18 => 0x0012
        0x00, 0x12,
        // priority=0 weight=0 port=5540
        0x00, 0x00,
        0x00, 0x00,
        0x15, 0xA4,
        // target: tiny.local
        0x04, 't','i','n','y',
        0x05, 'l','o','c','a','l',
        0x00,

        // ADDITIONAL 2: TXT for instance (one entry "D")
        0x04, 'T','i','n','y',
        0x08, '_','m','a','t','t','e','r','c',
        0x04, '_','u','d','p',
        0x05, 'l','o','c','a','l',
        0x00,
        0x00, 0x10, // TXT
        0x00, 0x01, // IN
        0x00, 0x00, 0x00, 0x78,
        0x00, 0x02, // RDLEN 2
        0x01, 'D',  // TXT: single string "D"

        // ADDITIONAL 3: AAAA for host tiny.local
        0x04, 't','i','n','y',
        0x05, 'l','o','c','a','l',
        0x00,
        0x00, 0x1C, // AAAA
        0x00, 0x01, // IN
        0x00, 0x00, 0x00, 0x78,
        0x00, 0x10, // RDLEN 16
        // 2001:0db8:0000:0000:0000:0000:0000:0001
        0x20, 0x01, 0x0d, 0xb8,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01,
    };

    TEST_ASSERT_EQUAL_INT(0, mdns_resolver_on_message(r, msg, sizeof(msg)));
    TEST_ASSERT_EQUAL_INT(1, spy.found_count);
    TEST_ASSERT_EQUAL_INT(1, spy.resolved_count);
    TEST_ASSERT_EQUAL_UINT16(5540, spy.last_port);
    TEST_ASSERT_EQUAL_STRING("tiny.local", spy.last_host);
    TEST_ASSERT_EQUAL_STRING("2001:db8:0:0:0:0:0:1", spy.last_addr0);
}

TEST(test_mdns_resolver_expires_entry_by_ttl)
{
    uint8_t storage[16384];
    ResolverSpy spy = {0};
    MdnsResolver *r = mdns_resolver_init(storage, sizeof(storage), spy_cb, &spy);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(0, mdns_resolver_start_browse(r, "_matterc._udp.local"));

    // now=1000ms
    TEST_ASSERT_EQUAL_INT(0, mdns_resolver_tick(r, 1000));

    // PTR answer with TTL=1s for the browse service -> instance "Tiny._matterc._udp.local"
    const uint8_t msg[] = {
        0x00, 0x00,
        0x84, 0x00,
        0x00, 0x00,
        0x00, 0x01,
        0x00, 0x00,
        0x00, 0x00,

        0x08, '_','m','a','t','t','e','r','c',
        0x04, '_','u','d','p',
        0x05, 'l','o','c','a','l',
        0x00,

        0x00, 0x0C,
        0x00, 0x01,
        // TTL=1
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x1A,

        0x04, 'T','i','n','y',
        0x08, '_','m','a','t','t','e','r','c',
        0x04, '_','u','d','p',
        0x05, 'l','o','c','a','l',
        0x00,
    };

    TEST_ASSERT_EQUAL_INT(0, mdns_resolver_on_message(r, msg, sizeof(msg)));
    TEST_ASSERT_EQUAL_INT(1, spy.found_count);
    TEST_ASSERT_EQUAL_INT(0, spy.expired_count);

    // Advance time beyond TTL (1000ms + 1000ms + epsilon)
    TEST_ASSERT_EQUAL_INT(0, mdns_resolver_tick(r, 2001));
    TEST_ASSERT_EQUAL_INT(1, spy.expired_count);
}

