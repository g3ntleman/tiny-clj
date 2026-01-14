/*
 * Copyright (c) 2020, Armink, <armink.ztl@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief utils
 *
 * Some utils for this library.
 */

#include "flash-tree.h"
#include "ft_low_lvl.h"
#include <stdio.h>
#include <string.h>

#define FDB_LOG_TAG "[utils]"

size_t _fdb_set_status(uint8_t status_table[], size_t status_num, size_t status_index) {
    size_t byte_index = SIZE_MAX;
    /*
     * | write garn |       status0       |       status1       |      status2         | status3 |
     * ------------------------------------------------------------------------------------------------------
     * |    1bit    | 0xFF                | 0x7F                |  0x3F                |  0x1F
     * ------------------------------------------------------------------------------------------------------
     * |    8bit    | 0xFF FF FF          | 0x00 FF FF          |  0x00 00 FF          |  0x00 00 00
     * ------------------------------------------------------------------------------------------------------
     * |   32bit    | 0xFFFFFFFF FFFFFFFF | 0x00FFFFFF FFFFFFFF |  0x00FFFFFF 00FFFFFF |  0x00FFFFFF
     * 00FFFFFF |            | 0xFFFFFFFF          | 0xFFFFFFFF          |  0xFFFFFFFF          |
     * 0x00FFFFFF
     * ------------------------------------------------------------------------------------------------------
     * |            | 0xFFFFFFFF FFFFFFFF | 0x00FFFFFF FFFFFFFF |  0x00FFFFFF FFFFFFFF |  0x00FFFFFF
     * FFFFFFFF |   64bit    | 0xFFFFFFFF FFFFFFFF | 0xFFFFFFFF FFFFFFFF |  0x00FFFFFF FFFFFFFF |
     * 0x00FFFFFF FFFFFFFF |            | 0xFFFFFFFF FFFFFFFF | 0xFFFFFFFF FFFFFFFF |  0xFFFFFFFF
     * FFFFFFFF |  0x00FFFFFF FFFFFFFF
     */
    memset(status_table, FDB_BYTE_ERASED, FDB_STATUS_TABLE_SIZE(status_num));
    if (status_index > 0) {
#if (FDB_WRITE_GRAN == 1)
        byte_index = (status_index - 1) / 8;
#if (FDB_BYTE_ERASED == 0xFF)
        status_table[byte_index] &= (0x00ff >> (status_index % 8));
#else
        status_table[byte_index] |= (0x00ff >> (status_index % 8));
#endif
#else
        byte_index = (status_index - 1) * (FDB_WRITE_GRAN / 8);
        status_table[byte_index] = FDB_BYTE_WRITTEN;
#endif /* FDB_WRITE_GRAN == 1 */
    }

    return byte_index;
}

size_t _fdb_get_status(uint8_t status_table[], size_t status_num) {
    size_t i = 0, status_num_bak = --status_num;

    while (status_num--) {
        /* get the first 0 position from end address to start address */
#if (FDB_WRITE_GRAN == 1)
        if ((status_table[status_num / 8] & (0x80 >> (status_num % 8))) == 0x00) {
            break;
        }
#else  /*  (FDB_WRITE_GRAN == 8) ||  (FDB_WRITE_GRAN == 32) ||  (FDB_WRITE_GRAN == 64) */
        if (status_table[status_num * FDB_WRITE_GRAN / 8] == FDB_BYTE_WRITTEN) {
            break;
        }
#endif /* FDB_WRITE_GRAN == 1 */
        i++;
    }

    return status_num_bak - i;
}

fdb_err_t _fdb_write_status(fdb_db_t db, uint32_t addr, uint8_t status_table[], size_t status_num,
                            size_t status_index, bool sync) {
    fdb_err_t result = FDB_NO_ERR;
    size_t byte_index;

    FDB_ASSERT(status_index < status_num);
    FDB_ASSERT(status_table);

    /* set the status first */
    byte_index = _fdb_set_status(status_table, status_num, status_index);

    /* the first status table value is all 1, so no need to write flash */
    if (byte_index == SIZE_MAX) {
        return FDB_NO_ERR;
    }
#if (FDB_WRITE_GRAN == 1)
    result = _fdb_flash_write(db, addr + byte_index, (uint32_t*)&status_table[byte_index], 1, sync);
#else  /*  (FDB_WRITE_GRAN == 8) ||  (FDB_WRITE_GRAN == 32) ||  (FDB_WRITE_GRAN == 64) */
    /* write the status by write granularity
     * some flash (like stm32 onchip) NOT supported repeated write before erase */
    result = _fdb_flash_write(db, addr + byte_index, (uint32_t*)&status_table[byte_index],
                              FDB_WRITE_GRAN / 8, sync);
#endif /* FDB_WRITE_GRAN == 1 */

    return result;
}

size_t _fdb_read_status(fdb_db_t db, uint32_t addr, uint8_t status_table[], size_t total_num) {
    FDB_ASSERT(status_table);

    _fdb_flash_read(db, addr, (uint32_t*)status_table, FDB_STATUS_TABLE_SIZE(total_num));

    return _fdb_get_status(status_table, total_num);
}

/*
 * find the continue 0xFF flash address to end address
 */
uint32_t _fdb_continue_ff_addr(fdb_db_t db, uint32_t start, uint32_t end) {
    uint8_t buf[32], last_data = FDB_BYTE_WRITTEN;
    size_t i, addr = start, read_size;

    for (; start < end; start += sizeof(buf)) {
        if (start + sizeof(buf) < end) {
            read_size = sizeof(buf);
        } else {
            read_size = end - start;
        }
        _fdb_flash_read(db, start, (uint32_t*)buf, read_size);
        for (i = 0; i < read_size; i++) {
            if (last_data != FDB_BYTE_ERASED && buf[i] == FDB_BYTE_ERASED) {
                addr = start + i;
            }
            last_data = buf[i];
        }
    }

    if (last_data == FDB_BYTE_ERASED) {
        return FDB_WG_ALIGN(addr);
    } else {
        return end;
    }
}

/**
 * Make a blob object.
 *
 * @param blob blob object
 * @param value_buf value buffer
 * @param buf_len buffer length
 *
 * @return new blob object
 */
fdb_blob_t fdb_blob_make(fdb_blob_t blob, const void* value_buf, size_t buf_len) {
    blob->buf = (void*)value_buf;
    blob->size = buf_len;

    return blob;
}

/**
 * Read the blob object in database.
 *
 * @param db database object
 * @param blob blob object
 *
 * @return read length
 */
size_t fdb_blob_read(fdb_db_t db, fdb_blob_t blob) {
    size_t read_len = blob->size;

    if (read_len > blob->saved.len) {
        read_len = blob->saved.len;
    }
    if (_fdb_flash_read(db, blob->saved.addr, blob->buf, read_len) != FDB_NO_ERR) {
        read_len = 0;
    }

    return read_len;
}

#ifdef FDB_USING_FILE_MODE
extern fdb_err_t _fdb_file_read(fdb_db_t db, uint32_t addr, void* buf, size_t size);
extern fdb_err_t _fdb_file_write(fdb_db_t db, uint32_t addr, const void* buf, size_t size,
                                 bool sync);
extern fdb_err_t _fdb_file_erase(fdb_db_t db, uint32_t addr, size_t size);
#endif /* FDB_USING_FILE_LIBC */

fdb_err_t _fdb_flash_read(fdb_db_t db, uint32_t addr, void* buf, size_t size) {
    fdb_err_t result = FDB_NO_ERR;

    if (db->file_mode) {
#ifdef FDB_USING_FILE_MODE
        return _fdb_file_read(db, addr, buf, size);
#else
        return FDB_READ_ERR;
#endif
    } else {
#ifdef FDB_USING_FAL_MODE
        if (fal_partition_read(db->storage.part, addr, (uint8_t*)buf, size) < 0) {
            result = FDB_READ_ERR;
        }
#endif
    }

    return result;
}

fdb_err_t _fdb_flash_erase(fdb_db_t db, uint32_t addr, size_t size) {
    fdb_err_t result = FDB_NO_ERR;

    if (db->file_mode) {
#ifdef FDB_USING_FILE_MODE
        return _fdb_file_erase(db, addr, size);
#else
        return FDB_ERASE_ERR;
#endif /* FDB_USING_FILE_MODE */
    } else {
#ifdef FDB_USING_FAL_MODE
        if (fal_partition_erase(db->storage.part, addr, size) < 0) {
            result = FDB_ERASE_ERR;
        }
#endif
    }

    return result;
}

fdb_err_t _fdb_flash_write(fdb_db_t db, uint32_t addr, const void* buf, size_t size, bool sync) {
    fdb_err_t result = FDB_NO_ERR;

    if (db->file_mode) {
#ifdef FDB_USING_FILE_MODE
        return _fdb_file_write(db, addr, buf, size, sync);
#else
        return FDB_WRITE_ERR;
#endif /* FDB_USING_FILE_MODE */
    } else {
#ifdef FDB_USING_FAL_MODE
        if (fal_partition_write(db->storage.part, addr, (uint8_t*)buf, size) < 0) {
            result = FDB_WRITE_ERR;
        }
#endif
    }

    return result;
}
