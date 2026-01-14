/*
 * FlashDB public header (local copy for flash-tree).
 *
 * NOTE: This is intentionally renamed from "flashdb.h" to "flash-tree.h"
 * to avoid leaking upstream naming into the rest of tiny-clj.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FLASH_TREE_FLASHDB_COMPAT_H
#define FLASH_TREE_FLASHDB_COMPAT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include "ft_cfg.h"

#ifdef FDB_USING_FAL_MODE
#include <fal.h>
#endif

#include "ft_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* FlashDB database API */
fdb_err_t fdb_tsdb_init   (fdb_tsdb_t db, const char *name, const char *path, fdb_get_time get_time, size_t max_len,
        void *user_data);
void      fdb_tsdb_control(fdb_tsdb_t db, int cmd, void *arg);
fdb_err_t fdb_tsdb_deinit(fdb_tsdb_t db);

/* blob API */
fdb_blob_t fdb_blob_make     (fdb_blob_t blob, const void *value_buf, size_t buf_len);
size_t     fdb_blob_read     (fdb_db_t db, fdb_blob_t blob);

/* Time series log API like a TSDB */
fdb_err_t  fdb_tsl_append      (fdb_tsdb_t db, fdb_blob_t blob);
fdb_err_t  fdb_tsl_append_with_ts(fdb_tsdb_t db, fdb_blob_t blob, fdb_time_t timestamp);
void       fdb_tsl_iter        (fdb_tsdb_t db, fdb_tsl_cb cb, void *cb_arg);
void       fdb_tsl_iter_reverse(fdb_tsdb_t db, fdb_tsl_cb cb, void *cb_arg);
void       fdb_tsl_iter_by_time(fdb_tsdb_t db, fdb_time_t from, fdb_time_t to, fdb_tsl_cb cb, void *cb_arg);
size_t     fdb_tsl_query_count (fdb_tsdb_t db, fdb_time_t from, fdb_time_t to, fdb_tsl_status_t status);
fdb_err_t  fdb_tsl_set_status  (fdb_tsdb_t db, fdb_tsl_t tsl, fdb_tsl_status_t status);
void       fdb_tsl_clean       (fdb_tsdb_t db);
fdb_blob_t fdb_tsl_to_blob     (fdb_tsl_t tsl, fdb_blob_t blob);

/* fdb_utils.c */
uint32_t   fdb_calc_crc32(uint32_t crc, const void *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_TREE_FLASHDB_COMPAT_H */

