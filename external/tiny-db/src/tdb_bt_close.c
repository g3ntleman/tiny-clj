/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Mike Olson.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)bt_close.c	8.3 (Berkeley) 2/21/94";
#endif /* LIBC_SCCS and not lint */

#include <sys/param.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tdb_bsd_db.h"
#include "tdb_bsd_btree.h"

static int bt_meta __P((BTREE*));

/*
 * BT_CLOSE -- Close a btree.
 *
 * Parameters:
 *	dbp:	pointer to access method
 *
 * Returns:
 *	RET_ERROR, RET_SUCCESS
 */
/**
 * @brief __bt_close.
 * @param dbp B-Tree database handle.
 * @return Return code (RET_SUCCESS on success).
 */
int __bt_close(DB* dbp) {
    BTREE* t;
    int fd;

    t = dbp->internal;

    /* Toss any page pinned across calls. */
    if (t->bt_pinned != NULL) {
        mpool_put(t->bt_mp, t->bt_pinned, 0);
        t->bt_pinned = NULL;
    }

    /*
     * Delete any already deleted record that we've been saving
     * because the cursor pointed to it.  Skip if cursor pgno is invalid
     * (e.g. seq never ran or cursor was reset).
     */
    if (ISSET(t, B_DELCRSR) && t->bt_bcursor.pgno != P_INVALID &&
        __bt_crsrdel(t, &t->bt_bcursor))
        return (RET_ERROR);

    if (__bt_sync(dbp, 0) == RET_ERROR)
        return (RET_ERROR);

    if (mpool_close(t->bt_mp) == RET_ERROR)
        return (RET_ERROR);

    if (t->bt_stack)
        free(t->bt_stack);
    if (t->bt_kbuf)
        free(t->bt_kbuf);
    if (t->bt_dbuf)
        free(t->bt_dbuf);

    fd = t->bt_fd;
    free(t);
    free(dbp);
    return (close(fd) ? RET_ERROR : RET_SUCCESS);
}

/*
 * BT_SYNC -- sync the btree to disk.
 *
 * Parameters:
 *	dbp:	pointer to access method
 *
 * Returns:
 *	RET_SUCCESS, RET_ERROR.
 */
/**
 * @brief __bt_sync.
 * @param dbp B-Tree database handle.
 * @param flags Option flags controlling operation behavior.
 * @return Return code (RET_SUCCESS on success).
 */
int __bt_sync(const DB* dbp, u_int flags) {
    BTREE* t;
    int status;
    PAGE* h;
    void* p = NULL;

    t = dbp->internal;

    /* Toss any page pinned across calls. */
    if (t->bt_pinned != NULL) {
        mpool_put(t->bt_mp, t->bt_pinned, 0);
        t->bt_pinned = NULL;
    }

    /* Sync doesn't currently take any flags. */
    if (flags != 0) {
        errno = EINVAL;
        return (RET_ERROR);
    }

    if (ISSET(t, B_INMEM | B_RDONLY) || !ISSET(t, B_MODIFIED))
        return (RET_SUCCESS);

    if (ISSET(t, B_METADIRTY) && bt_meta(t) == RET_ERROR)
        return (RET_ERROR);

    /*
     * Nastiness.  If the cursor has been marked for deletion, but not
     * actually deleted, we have to make a copy of the page, delete the
     * key/data item, sync the file, and then restore the original page
     * contents.  Skip if cursor pgno is invalid or page/index not valid.
     */
    if (ISSET(t, B_DELCRSR) && t->bt_bcursor.pgno != P_INVALID) {
        if ((p = (void*)malloc(t->bt_psize)) == NULL)
            return (RET_ERROR);
        if ((h = mpool_get(t->bt_mp, t->bt_bcursor.pgno, 0)) == NULL) {
            free(p);
            return (RET_ERROR);
        }
        if ((h->flags & P_TYPE) == P_BLEAF &&
            t->bt_bcursor.index < NEXTINDEX(h)) {
            memmove(p, h, t->bt_psize);
            if ((status = __bt_dleaf(t, h, t->bt_bcursor.index)) == RET_ERROR) {
                free(p);
                mpool_put(t->bt_mp, h, 0);
                return (RET_ERROR);
            }
            mpool_put(t->bt_mp, h, MPOOL_DIRTY);
        } else {
            mpool_put(t->bt_mp, h, 0);
            free(p);
            p = NULL;
        }
    }

    if ((status = mpool_sync(t->bt_mp)) == RET_SUCCESS)
        CLR(t, B_MODIFIED);

    if (ISSET(t, B_DELCRSR) && p != NULL) {
        if ((h = mpool_get(t->bt_mp, t->bt_bcursor.pgno, 0)) == NULL) {
            free(p);
            return (RET_ERROR);
        }
        memmove(h, p, t->bt_psize);
        free(p);
        p = NULL;
        mpool_put(t->bt_mp, h, MPOOL_DIRTY);
    }
    return (status);
}

/*
 * BT_META -- write the tree meta data to disk.
 *
 * Parameters:
 *	t:	tree
 *
 * Returns:
 *	RET_ERROR, RET_SUCCESS
 */
/**
 * @brief bt_meta.
 * @param t B-Tree context.
 * @return Return code (RET_SUCCESS on success).
 */
static int bt_meta(BTREE* t) {
    BTMETA m;
    void* p;

    if ((p = mpool_get(t->bt_mp, P_META, 0)) == NULL)
        return (RET_ERROR);

    /* Fill in metadata. */
    m.m_magic = BTREEMAGIC;
    m.m_version = BTREEVERSION;
    m.m_psize = t->bt_psize;
    m.m_free = t->bt_free;
    m.m_nrecs = t->bt_nrecs;
    m.m_flags = t->bt_flags & SAVEMETA;

    memmove(p, &m, sizeof(BTMETA));
    mpool_put(t->bt_mp, p, MPOOL_DIRTY);
    return (RET_SUCCESS);
}
