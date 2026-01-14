// ft_bsd_blockfile.h - Minimal blockdev-backed "file" port for 4.4BSD btree/mpool.
//
// This provides a tiny subset of POSIX-like file calls (open/read/write/lseek/fstat/fcntl/close)
// used by the 4.4BSD btree and mpool code, but backed by an ft_blockdev_t.
//
// The btree sources are compiled with preprocessor remaps (e.g. -Dopen=ft_bsd_open),
// so these symbols satisfy the btree's dependencies without pulling in real libc I/O.
//
// All comments in English (workspace rule).

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "ft_blockdev.h"

struct stat;

/* Global blockdev binding (single-threaded design) */
extern ft_blockdev_t* g_ft_bsd_bound_bdev;

/**
 * Bind a block device as the backing store for subsequent ft_bsd_open calls.
 * Single-threaded; the binding is global.
 */
static inline void ft_bsd_blockfile_bind(ft_blockdev_t* bdev) {
    g_ft_bsd_bound_bdev = bdev;
}

/**
 * Unbind the current block device.
 */
static inline void ft_bsd_blockfile_unbind(void) {
    g_ft_bsd_bound_bdev = NULL;
}

/**
 * Get the currently bound block device.
 */
static inline ft_blockdev_t* ft_bsd_blockfile_get_bdev(void) {
    return g_ft_bsd_bound_bdev;
}

int ft_bsd_open(const char* path, int flags, ...);
int ft_bsd_close(int fd);
ssize_t ft_bsd_read(int fd, void* buf, size_t nbyte);
ssize_t ft_bsd_write(int fd, const void* buf, size_t nbyte);
off_t ft_bsd_lseek(int fd, off_t offset, int whence);
int ft_bsd_fstat(int fd, struct stat* sb);
int ft_bsd_fcntl(int fd, int cmd, ...);
int ft_bsd_fsync(int fd);

