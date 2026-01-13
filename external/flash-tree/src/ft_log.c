// ft_log.c - Append-only record log with checkpoints.

#include "ft_log.h"

#include "ft_crc32.h"

#include <string.h>

// On-flash layout (little-endian, packed).
// NOTE: Keep this stable once persisted; for now it's test-only.
#define FT_LOG_MAGIC 0x474C5446u /* 'F''T''L''G' */
#define FT_LOG_VERSION 1u
#define FT_LOG_MAX_PAYLOAD 256u

typedef struct __attribute__((packed)) ft_log_hdr {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t payload_len;
    uint64_t seqno;
    uint32_t crc32; // over hdr (crc32=0) + payload
} ft_log_hdr_t;

static ft_status_t ft_log_crc32(const ft_log_hdr_t* hdr_in, const void* payload, uint32_t payload_len, uint32_t* out_crc)
{
    if (!hdr_in || (!payload && payload_len != 0) || !out_crc) return FT_ERR_INVALID_ARG;
    if (payload_len > FT_LOG_MAX_PAYLOAD) return FT_ERR_UNSUPPORTED;

    ft_log_hdr_t hdr = *hdr_in;
    hdr.crc32 = 0;

    if (payload_len == 0) {
        *out_crc = ft_crc32_ieee(&hdr, sizeof(hdr), 0);
        return FT_OK;
    }

    uint8_t buf[sizeof(hdr) + FT_LOG_MAX_PAYLOAD];
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), payload, payload_len);
    *out_crc = ft_crc32_ieee(buf, sizeof(hdr) + payload_len, 0);
    return FT_OK;
}

static ft_status_t read_bytes(const ft_blockdev_t* bdev, uint32_t off, void* out, size_t len) {
    return ft_blockdev_read(bdev, off, out, len);
}

static ft_status_t write_bytes(const ft_blockdev_t* bdev, uint32_t off, const void* data, size_t len) {
    return ft_blockdev_prog(bdev, off, data, len);
}

static ft_status_t ensure_erased_for_append(const ft_blockdev_t* bdev, uint32_t off, size_t len) {
    // For now, assume caller erased whole region in tests OR uses RAM backend.
    // We still support erasing whole device at init by calling this on (0, total).
    (void)bdev; (void)off; (void)len;
    return FT_OK;
}

ft_status_t ft_log_init(ft_log_t* log, const ft_blockdev_t* bdev) {
    if (!log || !bdev) return FT_ERR_INVALID_ARG;
    ft_status_t st = ft_blockdev_validate(bdev);
    if (st != FT_OK) return st;
    memset(log, 0, sizeof(*log));
    log->bdev = bdev;
    log->write_off = 0;
    log->next_seqno = 1;
    // Start from fully erased medium for deterministic tests.
    st = ft_blockdev_erase(bdev, 0, bdev->geom.total_size_bytes - (bdev->geom.total_size_bytes % bdev->geom.erase_granularity));
    if (st != FT_OK) return st;
    return FT_OK;
}

ft_status_t ft_log_append(ft_log_t* log, ft_log_rec_type_t type,
                          const void* payload, uint32_t payload_len,
                          uint32_t* out_rec_off, uint64_t* out_seqno) {
    if (!log || !log->bdev) return FT_ERR_INVALID_ARG;
    if (!payload && payload_len != 0) return FT_ERR_INVALID_ARG;

    const uint32_t rec_off = log->write_off;
    const uint32_t total = (uint32_t)sizeof(ft_log_hdr_t) + payload_len;
    if ((uint64_t)rec_off + (uint64_t)total > log->bdev->geom.total_size_bytes) return FT_ERR_IO;

    ft_status_t st = ensure_erased_for_append(log->bdev, rec_off, total);
    if (st != FT_OK) return st;

    ft_log_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = FT_LOG_MAGIC;
    hdr.version = FT_LOG_VERSION;
    hdr.type = (uint16_t)type;
    hdr.payload_len = payload_len;
    hdr.seqno = log->next_seqno;
    hdr.crc32 = 0;

    uint32_t crc = 0;
    st = ft_log_crc32(&hdr, payload, payload_len, &crc);
    if (st != FT_OK) return st;
    hdr.crc32 = crc;

    st = write_bytes(log->bdev, rec_off, &hdr, sizeof(hdr));
    if (st != FT_OK) return st;
    if (payload_len) {
        st = write_bytes(log->bdev, rec_off + (uint32_t)sizeof(hdr), payload, payload_len);
        if (st != FT_OK) return st;
    }

    log->write_off += total;
    log->next_seqno++;
    if (out_rec_off) *out_rec_off = rec_off;
    if (out_seqno) *out_seqno = hdr.seqno;
    return FT_OK;
}

ft_status_t ft_log_checkpoint(ft_log_t* log, ft_log_checkpoint_t cp, uint32_t* out_rec_off) {
    // Payload is the checkpoint struct.
    return ft_log_append(log, FT_LOG_REC_CHECKPOINT, &cp, (uint32_t)sizeof(cp), out_rec_off, NULL);
}

ft_status_t ft_log_recover_last_checkpoint(const ft_blockdev_t* bdev, ft_log_checkpoint_t* out_cp) {
    if (!bdev || !out_cp) return FT_ERR_INVALID_ARG;
    ft_status_t st = ft_blockdev_validate(bdev);
    if (st != FT_OK) return st;

    ft_log_checkpoint_t last = {0};

    uint32_t off = 0;
    while ((uint64_t)off + sizeof(ft_log_hdr_t) <= bdev->geom.total_size_bytes) {
        ft_log_hdr_t hdr;
        st = read_bytes(bdev, off, &hdr, sizeof(hdr));
        if (st != FT_OK) return st;

        // End of log: erased bytes (all 0xFF).
        const uint8_t* hb = (const uint8_t*)&hdr;
        int all_ff = 1;
        for (size_t i = 0; i < sizeof(hdr); i++) {
            if (hb[i] != 0xFF) { all_ff = 0; break; }
        }
        if (all_ff) break;

        if (hdr.magic != FT_LOG_MAGIC || hdr.version != FT_LOG_VERSION) break;
        if ((uint64_t)off + sizeof(ft_log_hdr_t) + hdr.payload_len > bdev->geom.total_size_bytes) break;

        // Read payload (small in tests).
        uint8_t payload_buf[FT_LOG_MAX_PAYLOAD];
        const void* payload_ptr = NULL;
        if (hdr.payload_len) {
            if (hdr.payload_len > sizeof(payload_buf)) return FT_ERR_UNSUPPORTED;
            st = read_bytes(bdev, off + (uint32_t)sizeof(hdr), payload_buf, hdr.payload_len);
            if (st != FT_OK) return st;
            payload_ptr = payload_buf;
        }

        // Validate CRC.
        uint32_t saved_crc = hdr.crc32;
        uint32_t crc = 0;
        st = ft_log_crc32(&hdr, payload_ptr, hdr.payload_len, &crc);
        if (st != FT_OK) return st;
        if (crc != saved_crc) break; // power loss / torn write

        if (hdr.type == FT_LOG_REC_CHECKPOINT && hdr.payload_len == sizeof(ft_log_checkpoint_t)) {
            memcpy(&last, payload_ptr, sizeof(last));
        }

        off += (uint32_t)sizeof(ft_log_hdr_t) + hdr.payload_len;
    }

    *out_cp = last;
    return FT_OK;
}

