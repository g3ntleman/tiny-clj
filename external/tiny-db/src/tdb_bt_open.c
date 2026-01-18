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
static char sccsid[] = "@(#)bt_open.c	8.5 (Berkeley) 2/21/94";
#endif /* LIBC_SCCS and not lint */

/*
 * Implementation of btree access method for 4.4BSD.
 *
 * The design here was originally based on that of the btree access method
 * used in the Postgres database system at UC Berkeley.  This implementation
 * is wholly independent of the Postgres code.
 */

#include <sys/param.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tdb_bsd_db.h"
#include "tdb_bsd_btree.h"

static int byteorder __P((void));
static int nroot __P((BTREE*));
/* tmp() is not used in tiny-db (no OS files). */

/*
 * __BT_OPEN -- Open a btree.
 *
 * Creates and fills a DB struct, and calls the routine that actually
 * opens the btree.
 *
 * Parameters:
 *	fname:	filename (NULL for in-memory trees)
 *	flags:	open flag bits
 *	mode:	open permission bits
 *	b:	BTREEINFO pointer
 *
 * Returns:
 *	NULL on failure, pointer to DB on success.
 *
 */
DB* __bt_open(const char* fname, int flags, int mode, const BTREEINFO* openinfo, int dflags) {
    struct stat sb;
    BTMETA m;
    BTREE* t;
    BTREEINFO b;
    DB* dbp;
    pgno_t ncache;
    int machine_lorder;

    t = NULL;

    /*
     * Intention is to make sure all of the user's selections are okay
     * here and then use them without checking.  Can't be complete, since
     * we don't know the right page size, lorder or flags until the backing
     * file is opened.  Also, the file's page size can cause the cachesize
     * to change.
     */
    machine_lorder = byteorder();
    if (openinfo) {
        b = *openinfo;

        /* Flags: R_DUP. */
        if (b.flags & ~(R_DUP))
            goto einval;

        /*
         * Page size must be indx_t aligned and >= MINPSIZE.  Default
         * page size is set farther on, based on the underlying file
         * transfer size.
         */
        if (b.psize &&
            (b.psize < MINPSIZE || b.psize > MAX_PAGE_OFFSET + 1 ||
             (b.psize & (sizeof(indx_t) - 1)) != 0))
            goto einval;

        /* Minimum number of keys per page; absolute minimum is 2. */
        if (b.minkeypage) {
            if (b.minkeypage < 2)
                goto einval;
        } else
            b.minkeypage = DEFMINKEYPAGE;

        /* If no comparison, use default comparison and prefix. */
        if (b.compare == NULL) {
            b.compare = __bt_defcmp;
            if (b.prefix == NULL)
                b.prefix = __bt_defpfx;
        }

        if (b.lorder == 0)
            b.lorder = machine_lorder;
    } else {
        b.compare = __bt_defcmp;
        b.cachesize = 0;
        b.flags = 0;
        b.lorder = machine_lorder;
        b.minkeypage = DEFMINKEYPAGE;
        b.prefix = __bt_defpfx;
        b.psize = 0;
    }

    /* Check for the ubiquitous PDP-11. */
    if (b.lorder != BIG_ENDIAN && b.lorder != LITTLE_ENDIAN)
        goto einval;

    /* Allocate and initialize DB and BTREE structures. */
    if ((t = (BTREE*)malloc(sizeof(BTREE))) == NULL)
        goto err;
    memset(t, 0, sizeof(BTREE));
    t->bt_bcursor.pgno = P_INVALID;
    t->bt_fd = -1; /* Don't close unopened fd on error. */
    t->bt_lorder = b.lorder;
    t->bt_order = NOT;
    t->bt_cmp = b.compare;
    t->bt_pfx = b.prefix;
    t->bt_rfd = -1;

    if ((t->bt_dbp = dbp = (DB*)malloc(sizeof(DB))) == NULL)
        goto err;
    t->bt_flags = 0;
    if (t->bt_lorder != machine_lorder)
        SET(t, B_NEEDSWAP);

    dbp->type = DB_BTREE;
    dbp->internal = t;
    dbp->close = __bt_close;
    dbp->del = __bt_delete;
    dbp->fd = __bt_fd;
    dbp->get = __bt_get;
    dbp->put = __bt_put;
    dbp->seq = __bt_seq;
    dbp->sync = __bt_sync;

    /*
     * tiny-db: storage is provided by mpool on a bound block device.
     * We do not open OS files here.
     */
    (void)fname;
    (void)flags;
    (void)mode;

    /* Choose page size (caller may override via openinfo->psize). */
    if (b.psize == 0)
        b.psize = MINPSIZE;
    if (b.psize < MINPSIZE)
        b.psize = MINPSIZE;
    if (b.psize > MAX_PAGE_OFFSET + 1)
        b.psize = MAX_PAGE_OFFSET + 1;

    /* Set flag if duplicates permitted. */
    if (!(b.flags & R_DUP))
        SET(t, B_NODUPS);

    t->bt_psize = b.psize;

    /* Fake stat for legacy code paths (unused after this point). */
    memset(&sb, 0, sizeof(sb));
    sb.st_blksize = b.psize;
    sb.st_size = 0;

    /* Set the cache size; must be a multiple of the page size. */
    if (b.cachesize && (b.cachesize & (b.psize - 1)) != 0)
        b.cachesize += (~b.cachesize & (b.psize - 1)) + 1;
    if (b.cachesize < b.psize * MINCACHE)
        b.cachesize = b.psize * MINCACHE;

    /* Calculate number of pages to cache. */
    ncache = (b.cachesize + t->bt_psize - 1) / t->bt_psize;

    /*
     * The btree data structure requires that at least two keys can fit on
     * a page, but other than that there's no fixed requirement.  The user
     * specified a minimum number per page, and we translated that into the
     * number of bytes a key/data pair can use before being placed on an
     * overflow page.  This calculation includes the page header, the size
     * of the index referencing the leaf item and the size of the leaf item
     * structure.  Also, don't let the user specify a minkeypage such that
     * a key/data pair won't fit even if both key and data are on overflow
     * pages.
     */
    t->bt_ovflsize = (t->bt_psize - BTDATAOFF) / b.minkeypage - (sizeof(indx_t) + NBLEAFDBT(0, 0));
    if (t->bt_ovflsize < NBLEAFDBT(NOVFLSIZE, NOVFLSIZE) + sizeof(indx_t))
        t->bt_ovflsize = NBLEAFDBT(NOVFLSIZE, NOVFLSIZE) + sizeof(indx_t);

    /* Initialize the buffer pool. */
    if ((t->bt_mp = mpool_open(NULL, t->bt_fd, t->bt_psize, ncache)) == NULL)
        goto err;
    mpool_filter(t->bt_mp, __bt_pgin, __bt_pgout, t);

    /*
     * Load meta-data from page 0 (P_META). If it doesn't exist yet, this is
     * a new tree.
     */
    if ((void*)0 != (void*)0) {
        /* dummy to keep patch context stable */
    }
    {
        void* p = mpool_get(t->bt_mp, P_META, 0);
        if (p) {
            memmove(&m, p, sizeof(BTMETA));
            (void)mpool_put(t->bt_mp, p, 0);

            /* Meta is already in host order after mpool_filter+pgin. */
            if (m.m_magic != BTREEMAGIC || m.m_version != BTREEVERSION)
                goto eftype;
            if (m.m_psize < MINPSIZE || m.m_psize > MAX_PAGE_OFFSET + 1 ||
                (m.m_psize & (sizeof(indx_t) - 1)) != 0)
                goto eftype;
            if (m.m_flags & ~SAVEMETA)
                goto eftype;

            t->bt_flags |= m.m_flags;
            t->bt_psize = m.m_psize;
            t->bt_free = m.m_free;
            t->bt_nrecs = m.m_nrecs;
        } else {
            t->bt_free = P_INVALID;
            t->bt_nrecs = 0;
            SET(t, B_METADIRTY);
        }
    }

    /* Create a root page if new tree. */
    if (nroot(t) == RET_ERROR)
        goto err;

    /* Global flags. */
    if (dflags & DB_LOCK)
        SET(t, B_DB_LOCK);
    if (dflags & DB_SHMEM)
        SET(t, B_DB_SHMEM);
    if (dflags & DB_TXN)
        SET(t, B_DB_TXN);

    return (dbp);

einval:
    errno = EINVAL;
    goto err;

eftype:
    errno = EFTYPE;
    goto err;

err:
    if (t) {
        if (t->bt_dbp)
            free(t->bt_dbp);
        if (t->bt_fd != -1)
            (void)close(t->bt_fd);
        free(t);
    }
    return (NULL);
}

/*
 * NROOT -- Create the root of a new tree.
 *
 * Parameters:
 *	t:	tree
 *
 * Returns:
 *	RET_ERROR, RET_SUCCESS
 */
static int nroot(BTREE* t) {
    PAGE *meta, *root;
    pgno_t npg;

    if ((meta = mpool_get(t->bt_mp, 0, 0)) != NULL) {
        mpool_put(t->bt_mp, meta, 0);
        return (RET_SUCCESS);
    }
    if (errno != EINVAL)
        return (RET_ERROR);

    if ((meta = mpool_new(t->bt_mp, &npg)) == NULL)
        return (RET_ERROR);

    if ((root = mpool_new(t->bt_mp, &npg)) == NULL)
        return (RET_ERROR);

    if (npg != P_ROOT)
        return (RET_ERROR);
    root->pgno = npg;
    root->prevpg = root->nextpg = P_INVALID;
    root->lower = BTDATAOFF;
    root->upper = t->bt_psize;
    root->flags = P_BLEAF;
    memset(meta, 0, t->bt_psize);
    mpool_put(t->bt_mp, meta, MPOOL_DIRTY);
    mpool_put(t->bt_mp, root, MPOOL_DIRTY);
    return (RET_SUCCESS);
}

static int byteorder(void) {
    u_int32_t x;
    u_char* p;

    x = 0x01020304;
    p = (u_char*)&x;
    switch (*p) {
    case 1:
        return (BIG_ENDIAN);
    case 4:
        return (LITTLE_ENDIAN);
    default:
        return (0);
    }
}

int __bt_fd(const DB* dbp) {
    BTREE* t;

    t = dbp->internal;

    /* Toss any page pinned across calls. */
    if (t->bt_pinned != NULL) {
        mpool_put(t->bt_mp, t->bt_pinned, 0);
        t->bt_pinned = NULL;
    }

    /* In-memory database can't have a file descriptor. */
    if (ISSET(t, B_INMEM)) {
        errno = ENOENT;
        return (-1);
    }
    return (t->bt_fd);
}
