/*
 * mDNS/DNS-SD codec tests
 *
 * Focus: RFC1035 name decoding (incl. compression pointers) and basic PTR query encoding.
 */

#include "tests_common.h"
#include "../mdns_codec.h"

TEST(test_mdns_decode_name_uncompressed)
{
    const uint8_t msg[] = {
        // DNS header (12 bytes)
        0x00, 0x00, // id
        0x00, 0x00, // flags
        0x00, 0x01, // qdcount
        0x00, 0x00, // ancount
        0x00, 0x00, // nscount
        0x00, 0x00, // arcount
        // QNAME: foo.local
        0x03, 'f', 'o', 'o',
        0x05, 'l', 'o', 'c', 'a', 'l',
        0x00,
        // QTYPE=A, QCLASS=IN
        0x00, 0x01,
        0x00, 0x01,
    };

    size_t off = 12;
    char name[256];
    int rc = mdns_decode_name(msg, sizeof(msg), &off, name, sizeof(name));
    TEST_ASSERT_EQUAL_INT(MDNS_CODEC_OK, rc);
    TEST_ASSERT_EQUAL_STRING("foo.local", name);
    // Offset should now be at the first byte after the terminating 0x00.
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(12 + 1 + 3 + 1 + 5 + 1), (uint32_t)off);
}

TEST(test_mdns_decode_name_with_compression_pointer)
{
    uint8_t msg[64];
    memset(msg, 0, sizeof(msg));

    // Write a minimal header.
    // QDCOUNT=1.
    msg[4] = 0x00;
    msg[5] = 0x01;

    // At offset 12: foo.local (same as previous test)
    size_t p = 12;
    msg[p++] = 0x03; msg[p++] = 'f'; msg[p++] = 'o'; msg[p++] = 'o';
    msg[p++] = 0x05; msg[p++] = 'l'; msg[p++] = 'o'; msg[p++] = 'c'; msg[p++] = 'a'; msg[p++] = 'l';
    msg[p++] = 0x00;
    // QTYPE/QCLASS
    msg[p++] = 0x00; msg[p++] = 0x01;
    msg[p++] = 0x00; msg[p++] = 0x01;

    // Next name: bar.foo.local using a pointer to offset 12.
    size_t name2_off = p;
    msg[p++] = 0x03; msg[p++] = 'b'; msg[p++] = 'a'; msg[p++] = 'r';
    msg[p++] = 0xC0; msg[p++] = 0x0C; // pointer to 12

    size_t off = name2_off;
    char name[256];
    int rc = mdns_decode_name(msg, sizeof(msg), &off, name, sizeof(name));
    TEST_ASSERT_EQUAL_INT(MDNS_CODEC_OK, rc);
    TEST_ASSERT_EQUAL_STRING("bar.foo.local", name);
    // Stream offset should advance by exactly the encoded bytes: 1+3 + 2
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(name2_off + 6), (uint32_t)off);
}

TEST(test_mdns_build_ptr_query_basic)
{
    uint8_t buf[512];
    size_t out_len = 0;

    int rc = mdns_build_ptr_query("_matterc._udp.local", buf, sizeof(buf), &out_len);
    TEST_ASSERT_EQUAL_INT(MDNS_CODEC_OK, rc);
    TEST_ASSERT_TRUE(out_len > 12);

    // Header: ID=0, QDCOUNT=1
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[4]);
    TEST_ASSERT_EQUAL_UINT8(0x01, buf[5]);

    // QTYPE/QCLASS at end: PTR=12, IN=1
    TEST_ASSERT_TRUE(out_len >= 16);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[out_len - 4]);
    TEST_ASSERT_EQUAL_UINT8(0x0C, buf[out_len - 3]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[out_len - 2]);
    TEST_ASSERT_EQUAL_UINT8(0x01, buf[out_len - 1]);
}

