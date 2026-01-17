#include "mdns_codec.h"

#include <string.h>

static inline void write_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

int mdns_decode_name(const uint8_t *msg, size_t msg_len,
                     size_t *offset_inout,
                     char *out, size_t out_cap) {
    if (!msg || !offset_inout || !out || out_cap == 0) return MDNS_CODEC_ERR_INVALID_ARG;
    size_t off = *offset_inout;
    if (off >= msg_len) return MDNS_CODEC_ERR_TRUNCATED;

    size_t out_pos = 0;
    size_t jumps = 0;
    int jumped = 0;
    size_t stream_end_off = 0;

    // RFC1035 name: sequence of labels ending with zero-length label.
    for (;;) {
        if (off >= msg_len) return MDNS_CODEC_ERR_TRUNCATED;
        uint8_t len = msg[off];

        // Compression pointer: 11xxxxxx xxxxxxxx
        if ((len & 0xC0u) == 0xC0u) {
            if (off + 1 >= msg_len) return MDNS_CODEC_ERR_TRUNCATED;
            uint16_t ptr = (uint16_t)(((uint16_t)(len & 0x3Fu) << 8) | (uint16_t)msg[off + 1]);
            if (ptr >= msg_len) return MDNS_CODEC_ERR_TRUNCATED;

            if (!jumped) {
                stream_end_off = off + 2;
                jumped = 1;
            }

            // Basic loop protection: cap number of pointer hops.
            jumps++;
            if (jumps > msg_len) return MDNS_CODEC_ERR_POINTER_LOOP;

            off = (size_t)ptr;
            continue;
        }

        // Reserved label types not supported.
        if ((len & 0xC0u) != 0) return MDNS_CODEC_ERR_BAD_NAME;

        // End of name.
        if (len == 0) {
            if (!jumped) stream_end_off = off + 1;
            break;
        }

        // Label length sanity.
        if (len > 63) return MDNS_CODEC_ERR_BAD_NAME;
        if (off + 1 + (size_t)len > msg_len) return MDNS_CODEC_ERR_TRUNCATED;

        // Add dot separator if not first label.
        if (out_pos != 0) {
            if (out_pos + 1 >= out_cap) return MDNS_CODEC_ERR_OUTPUT_TOO_SMALL;
            out[out_pos++] = '.';
        }

        // Copy label.
        if (out_pos + (size_t)len >= out_cap) return MDNS_CODEC_ERR_OUTPUT_TOO_SMALL;
        memcpy(out + out_pos, msg + off + 1, (size_t)len);
        out_pos += (size_t)len;

        off += 1 + (size_t)len;
        if (out_pos > 255) return MDNS_CODEC_ERR_NAME_TOO_LONG;
    }

    // NUL-terminate.
    if (out_pos >= out_cap) return MDNS_CODEC_ERR_OUTPUT_TOO_SMALL;
    out[out_pos] = '\0';

    *offset_inout = stream_end_off;
    return MDNS_CODEC_OK;
}

int mdns_encode_qname(const char *name, uint8_t *out, size_t out_cap, size_t *out_len) {
    if (!name || !out || !out_len) return MDNS_CODEC_ERR_INVALID_ARG;
    size_t nlen = strlen(name);
    if (nlen == 0) return MDNS_CODEC_ERR_BAD_NAME;

    size_t pos = 0;
    size_t i = 0;
    while (i < nlen) {
        // Skip leading dots (tolerate).
        if (name[i] == '.') { i++; continue; }

        // Find end of label.
        size_t start = i;
        while (i < nlen && name[i] != '.') i++;
        size_t lab_len = i - start;
        if (lab_len == 0) continue;
        if (lab_len > 63) return MDNS_CODEC_ERR_BAD_NAME;

        if (pos + 1 + lab_len >= out_cap) return MDNS_CODEC_ERR_OUTPUT_TOO_SMALL;
        out[pos++] = (uint8_t)lab_len;
        memcpy(out + pos, name + start, lab_len);
        pos += lab_len;
    }

    if (pos + 1 > out_cap) return MDNS_CODEC_ERR_OUTPUT_TOO_SMALL;
    out[pos++] = 0; // root label terminator
    if (pos > 255) return MDNS_CODEC_ERR_NAME_TOO_LONG;

    *out_len = pos;
    return MDNS_CODEC_OK;
}

int mdns_build_ptr_query(const char *qname, uint8_t *out, size_t out_cap, size_t *out_len) {
    if (!qname || !out || !out_len) return MDNS_CODEC_ERR_INVALID_ARG;

    // DNS header is 12 bytes.
    if (out_cap < 12) return MDNS_CODEC_ERR_OUTPUT_TOO_SMALL;
    memset(out, 0, 12);

    // mDNS uses ID=0.
    write_be16(out + 0, 0);
    // flags=0 (standard query).
    write_be16(out + 2, 0);
    // QDCOUNT=1
    write_be16(out + 4, 1);
    // AN/NS/AR=0
    write_be16(out + 6, 0);
    write_be16(out + 8, 0);
    write_be16(out + 10, 0);

    size_t qname_len = 0;
    int rc = mdns_encode_qname(qname, out + 12, out_cap - 12, &qname_len);
    if (rc != MDNS_CODEC_OK) return rc;

    size_t pos = 12 + qname_len;
    if (pos + 4 > out_cap) return MDNS_CODEC_ERR_OUTPUT_TOO_SMALL;

    // QTYPE=PTR (12), QCLASS=IN (1)
    write_be16(out + pos + 0, 12);
    write_be16(out + pos + 2, 1);
    pos += 4;

    *out_len = pos;
    return MDNS_CODEC_OK;
}

