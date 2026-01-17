#ifndef TINY_CLJ_MDNS_CODEC_H
#define TINY_CLJ_MDNS_CODEC_H

#include <stddef.h>
#include <stdint.h>

// Minimal mDNS/DNS-SD codec helpers.
// Design goals:
// - No heap allocation in the codec layer.
// - Decode DNS names with RFC1035 compression pointers.
// - Encode QNAME and build a basic PTR query (useful for browsing).

typedef enum MdnsCodecStatus {
    MDNS_CODEC_OK = 0,
    MDNS_CODEC_ERR_INVALID_ARG = -1,
    MDNS_CODEC_ERR_TRUNCATED = -2,
    MDNS_CODEC_ERR_NAME_TOO_LONG = -3,
    MDNS_CODEC_ERR_BAD_NAME = -4,
    MDNS_CODEC_ERR_POINTER_LOOP = -5,
    MDNS_CODEC_ERR_OUTPUT_TOO_SMALL = -6,
} MdnsCodecStatus;

// Decode a DNS name at *offset_inout inside msg into out (NUL-terminated).
// - Updates *offset_inout to the first byte after the name in the original message stream.
// - Output format: "foo.bar.local" (no trailing dot).
int mdns_decode_name(const uint8_t *msg, size_t msg_len,
                     size_t *offset_inout,
                     char *out, size_t out_cap);

// Encode a DNS name ("foo.bar.local" or "foo.bar.local.") into label format.
// Writes bytes to out and sets *out_len to the encoded length.
int mdns_encode_qname(const char *name, uint8_t *out, size_t out_cap, size_t *out_len);

// Build a minimal mDNS query message (ID=0, one question) for a PTR record.
// qname should be the full service name, e.g. "_matterc._udp.local".
int mdns_build_ptr_query(const char *qname, uint8_t *out, size_t out_cap, size_t *out_len);

#endif // TINY_CLJ_MDNS_CODEC_H

