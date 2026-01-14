// ft_bsd_blockfile.c - Blockdev-backed "file" for 4.4BSD btree/mpool.
//
// Provides POSIX-like file operations (open/read/write/lseek/fstat/close)
// backed by a flash block device, allowing BSD btree code to work with flash
// storage without modification.
//
// Design:
// - Small header region at offset 0 stores file metadata
// - File data starts after header (at data_base offset)
// - Writes use Read-Modify-Write for erase-block alignment

#include "ft_bsd_blockfile.h"
#include "ft_utils.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Small, fixed FD table; only a handful of opens happen in our usage */
#define FT_BSD_MAX_FD 8

/* Header stored at blockdev offset 0, data starts at hdr_region_len */
/* NOTE: btree page 0 (BTMETA) lives at file offset 0, which maps to data_base */
#define FT_BSD_HDR_MAGIC 0x46544246u   /* 'FTBF' */
#define FT_BSD_HDR_VERSION 1u

/**
 * On-flash header structure for BSD blockfile.
 */
typedef struct ft_bsd_hdr {
    uint32_t magic;          /* Magic number for validation */
    uint32_t version;        /* Format version */
    uint32_t file_size;      /* Bytes in the btree file region */
    uint32_t hdr_region_len; /* Bytes reserved at offset 0 */
} ft_bsd_hdr_t;

/**
 * File descriptor state for an open blockfile.
 */
typedef struct ft_bsd_fd {
    int in_use;              /* FD is allocated */
    ft_blockdev_t* bdev;     /* Backing block device */
    uint32_t off;            /* Current file position */
    uint32_t size;           /* Current file size */
    uint32_t hdr_region_len; /* Header region size */
    uint32_t data_base;      /* Start of data region */
    
    /* Scratch buffer for erase-block RMW writes */
    uint8_t* blk_buf;
    size_t blk_cap;
} ft_bsd_fd_t;

/* Global state (single-threaded design) */
ft_blockdev_t* g_ft_bsd_bound_bdev = NULL;  /* Exposed for inline functions */
static ft_bsd_fd_t g_fds[FT_BSD_MAX_FD];

/**
 * Validate file descriptor.
 */
static int ft_fd_is_valid(int fd) { 
    return fd >= 0 && fd < FT_BSD_MAX_FD && g_fds[fd].in_use; 
}

/**
 * Read header from block device.
 */
static ft_status_t ft_read_hdr(ft_blockdev_t* bdev, ft_bsd_hdr_t* out) {
    if (!bdev || !out) return FT_ERR_INVALID_ARG;
    ft_bsd_hdr_t h = {0};
    ft_status_t st = ft_blockdev_read(bdev, 0, &h, sizeof(h));
    if (st != FT_OK) return st;
    *out = h;
    return FT_OK;
}

/**
 * Write header to block device (erases and programs header region).
 */
static ft_status_t ft_write_hdr(ft_blockdev_t* bdev, const ft_bsd_hdr_t* h) {
    if (!bdev || !h) return FT_ERR_INVALID_ARG;

    const uint32_t eg = bdev->geom.erase_granularity ? bdev->geom.erase_granularity : 1;
    uint32_t hdr_region_len = h->hdr_region_len;
    if (hdr_region_len == 0) {
        hdr_region_len = ft_align_up_u32(ft_max_u32(64u, eg), eg);
    }

    /* Write header by erasing + programming the entire header region */
    uint8_t* buf = (uint8_t*)malloc(hdr_region_len);
    if (!buf) return FT_ERR_NO_MEMORY;
    memset(buf, 0xFF, hdr_region_len);
    memcpy(buf, h, sizeof(*h));

    ft_status_t st = ft_blockdev_erase(bdev, 0, hdr_region_len);
    if (st == FT_OK) st = ft_blockdev_prog(bdev, 0, buf, hdr_region_len);
    free(buf);
    return st;
}

/**
 * Ensure block buffer has sufficient capacity for RMW operations.
 */
static ft_status_t ft_ensure_blk_buf(ft_bsd_fd_t* f, size_t need) {
    if (need <= f->blk_cap) return FT_OK;
    uint8_t* p = (uint8_t*)realloc(f->blk_buf, need);
    if (!p) return FT_ERR_NO_MEMORY;
    f->blk_buf = p;
    f->blk_cap = need;
    return FT_OK;
}

/**
 * Perform Read-Modify-Write operation for unaligned writes.
 * 
 * Flash devices require erase-then-program operations. This function
 * handles writes that don't align to erase block boundaries by:
 * 1. Reading the full erase block(s) affected
 * 2. Modifying the relevant bytes
 * 3. Erasing and reprogramming the entire block(s)
 */
static ft_status_t ft_rmw_write(ft_blockdev_t* bdev, uint32_t addr, 
                                 const void* data, size_t len, ft_bsd_fd_t* f) {
    if (!bdev || (!data && len != 0) || !f) return FT_ERR_INVALID_ARG;
    if (len == 0) return FT_OK;

    const uint32_t eg = bdev->geom.erase_granularity ? bdev->geom.erase_granularity : 1;
    ft_status_t st = ft_ensure_blk_buf(f, eg);
    if (st != FT_OK) return st;

    const uint8_t* in = (const uint8_t*)data;
    uint32_t start = addr;
    uint32_t end = addr + (uint32_t)len;
    uint32_t blk0 = start - (start % eg);
    uint32_t blkN = (end - 1u) - ((end - 1u) % eg);

    /* Process each affected erase block */
    for (uint32_t blk = blk0; blk <= blkN; blk += eg) {
        /* Read whole erase block */
        st = ft_blockdev_read(bdev, blk, f->blk_buf, eg);
        if (st != FT_OK) return st;

        /* Apply patch within this block */
        uint32_t lo = (start > blk) ? (start - blk) : 0;
        uint32_t hi = (end < (blk + eg)) ? (end - blk) : eg;
        size_t n = (hi > lo) ? (size_t)(hi - lo) : 0;
        if (n) {
            size_t in_off = (size_t)((blk + lo) - start);
            memcpy(f->blk_buf + lo, in + in_off, n);
        }

        /* Rewrite: erase + program full block */
        st = ft_blockdev_erase(bdev, blk, eg);
        if (st != FT_OK) return st;
        st = ft_blockdev_prog(bdev, blk, f->blk_buf, eg);
        if (st != FT_OK) return st;
    }
    return FT_OK;
}

int ft_bsd_open(const char* path, int flags, ...) {
    (void)path;
    (void)flags;

    if (!g_ft_bsd_bound_bdev) {
        errno = EINVAL;
        return -1;
    }
    if (ft_blockdev_validate(g_ft_bsd_bound_bdev) != FT_OK) {
        errno = EIO;
        return -1;
    }

    int fd = -1;
    for (int i = 0; i < FT_BSD_MAX_FD; i++) {
        if (!g_fds[i].in_use) { fd = i; break; }
    }
    if (fd < 0) { errno = EMFILE; return -1; }

    /* Establish header region size (at least 64 bytes, erase-gran aligned) */
    const uint32_t eg = g_ft_bsd_bound_bdev->geom.erase_granularity ? 
                        g_ft_bsd_bound_bdev->geom.erase_granularity : 1;
    uint32_t hdr_region_len = ft_align_up_u32(ft_max_u32(64u, eg), eg);
    if (hdr_region_len >= g_ft_bsd_bound_bdev->geom.total_size_bytes) { 
        errno = ENOSPC; 
        return -1; 
    }

    ft_bsd_hdr_t h = {0};
    ft_status_t st = ft_read_hdr(g_ft_bsd_bound_bdev, &h);
    if (st != FT_OK) { errno = EIO; return -1; }

    uint32_t size = 0;
    if (h.magic == FT_BSD_HDR_MAGIC && h.version == FT_BSD_HDR_VERSION && 
        h.hdr_region_len == hdr_region_len) {
        size = h.file_size;
    } else {
        /* Uninitialized/new: write initial header */
        ft_bsd_hdr_t nh = { 
            .magic = FT_BSD_HDR_MAGIC, 
            .version = FT_BSD_HDR_VERSION, 
            .file_size = 0, 
            .hdr_region_len = hdr_region_len 
        };
        st = ft_write_hdr(g_ft_bsd_bound_bdev, &nh);
        if (st != FT_OK) { errno = EIO; return -1; }
        size = 0;
    }

    memset(&g_fds[fd], 0, sizeof(g_fds[fd]));
    g_fds[fd].in_use = 1;
    g_fds[fd].bdev = g_ft_bsd_bound_bdev;
    g_fds[fd].off = 0;
    g_fds[fd].size = size;
    g_fds[fd].hdr_region_len = hdr_region_len;
    g_fds[fd].data_base = hdr_region_len;
    return fd;
}

int ft_bsd_close(int fd) {
    if (!ft_fd_is_valid(fd)) { errno = EBADF; return -1; }
    ft_bsd_fd_t* f = &g_fds[fd];
    free(f->blk_buf);
    memset(f, 0, sizeof(*f));
    return 0;
}

ssize_t ft_bsd_read(int fd, void* buf, size_t nbyte) {
    if (!ft_fd_is_valid(fd)) { errno = EBADF; return -1; }
    ft_bsd_fd_t* f = &g_fds[fd];
    if (!buf && nbyte != 0) { errno = EINVAL; return -1; }
    if (nbyte == 0) return 0;
    if (f->off >= f->size) return 0;

    size_t want = nbyte;
    size_t remain = (size_t)(f->size - f->off);
    if (want > remain) want = remain;

    uint32_t addr = f->data_base + f->off;
    ft_status_t st = ft_blockdev_read(f->bdev, addr, buf, want);
    if (st != FT_OK) { errno = EIO; return -1; }
    f->off += (uint32_t)want;
    return (ssize_t)want;
}

ssize_t ft_bsd_write(int fd, const void* buf, size_t nbyte) {
    if (!ft_fd_is_valid(fd)) { errno = EBADF; return -1; }
    ft_bsd_fd_t* f = &g_fds[fd];
    if (!buf && nbyte != 0) { errno = EINVAL; return -1; }
    if (nbyte == 0) return 0;

    uint32_t addr = f->data_base + f->off;
    uint32_t end = addr + (uint32_t)nbyte;
    if (end > f->bdev->geom.total_size_bytes) { errno = ENOSPC; return -1; }

    ft_status_t st = ft_rmw_write(f->bdev, addr, buf, nbyte, f);
    if (st != FT_OK) { errno = EIO; return -1; }

    f->off += (uint32_t)nbyte;
    uint32_t new_size = (f->off > f->size) ? f->off : f->size;
    if (new_size != f->size) {
        f->size = new_size;
        /* Persist size in header (rewrite header region) */
        ft_bsd_hdr_t nh = { 
            .magic = FT_BSD_HDR_MAGIC, 
            .version = FT_BSD_HDR_VERSION, 
            .file_size = f->size, 
            .hdr_region_len = f->hdr_region_len 
        };
        st = ft_write_hdr(f->bdev, &nh);
        if (st != FT_OK) { errno = EIO; return -1; }
    }
    return (ssize_t)nbyte;
}

off_t ft_bsd_lseek(int fd, off_t offset, int whence) {
    if (!ft_fd_is_valid(fd)) { errno = EBADF; return (off_t)-1; }
    ft_bsd_fd_t* f = &g_fds[fd];
    int64_t base = 0;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = (int64_t)f->off;
    else if (whence == SEEK_END) base = (int64_t)f->size;
    else { errno = EINVAL; return (off_t)-1; }

    int64_t noff = base + (int64_t)offset;
    if (noff < 0) { errno = EINVAL; return (off_t)-1; }
    if (noff > (int64_t)0x7FFFFFFF) { errno = EINVAL; return (off_t)-1; }
    f->off = (uint32_t)noff;
    return (off_t)f->off;
}

int ft_bsd_fstat(int fd, struct stat* sb) {
    if (!ft_fd_is_valid(fd)) { errno = EBADF; return -1; }
    if (!sb) { errno = EINVAL; return -1; }
    ft_bsd_fd_t* f = &g_fds[fd];

    memset(sb, 0, sizeof(*sb));
    sb->st_mode = S_IFREG | 0600;
    sb->st_size = (off_t)f->size;
    sb->st_blksize = (blksize_t)ft_max_u32(512u, 
        f->bdev->geom.erase_granularity ? f->bdev->geom.erase_granularity : 1u);
    return 0;
}

int ft_bsd_fcntl(int fd, int cmd, ...) {
    if (!ft_fd_is_valid(fd)) { errno = EBADF; return -1; }
    // btree uses F_SETFD for close-on-exec; ignore.
    (void)cmd;
    return 0;
}

int ft_bsd_fsync(int fd) {
    if (!ft_fd_is_valid(fd)) { errno = EBADF; return -1; }
    // Our "file" writes are applied synchronously to the blockdev.
    return 0;
}

