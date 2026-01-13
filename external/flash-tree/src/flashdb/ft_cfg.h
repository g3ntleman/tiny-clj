/*
 * FlashDB configuration for Flash-Tree TSDB port.
 *
 * This file is only used to build FlashDB's TSDB module as part of flash-tree.
 * Keep it minimal and deterministic for host tests.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FDB_CFG_H
#define FDB_CFG_H

/* Enable TSDB module. */
#define FDB_USING_TSDB

/* Enable KVDB module (crash-safe KV + GC/compaction). */
#define FDB_USING_KVDB

/* Use 64-bit timestamps (Flash-Tree uses uint64_t for ft_time_t). */
#define FDB_USING_TIMESTAMP_64BIT

/* Force "file mode" codepaths, but we provide our own in-memory/bdev-backed
 * _fdb_file_read/_write/_erase implementation in flash-tree.
 */
#define FDB_USING_FILE_MODE

/* Write granularity in bits. 8-bit keeps the status tables byte-based and
 * matches typical NOR byte programming used in host RAM backends.
 */
#define FDB_WRITE_GRAN 8

/* Allow reasonably long path-like keys from tiny-clj/fs_layer. */
#ifndef FDB_KV_NAME_MAX
#define FDB_KV_NAME_MAX 255
#endif

/* Disable FlashDB logging to keep tiny-clj output quiet and reduce deps. */
#ifndef FDB_PRINT
#define FDB_PRINT(...) do { } while (0)
#endif

#endif /* FDB_CFG_H */

