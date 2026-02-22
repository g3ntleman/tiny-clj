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
static char sccsid[] = "@(#)bt_put.c	8.3 (Berkeley) 9/16/93";
#endif /* LIBC_SCCS and not lint */

#include <sys/types.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tdb_bsd_db.h"
#include "tdb_bsd_btree.h"

static EPG* bt_fast __P((BTREE*, const DBT*, const DBT*, int*));

/*
 * __BT_PUT -- Add a btree item to the tree.
 *
 * Parameters:
 *	dbp:	pointer to access method
 *	key:	key
 *	data:	data
 *	flag:	R_NOOVERWRITE
 *
 * Returns:
 *	RET_ERROR, RET_SUCCESS and RET_SPECIAL if the key is already in the
 *	tree and R_NOOVERWRITE specified.
 */
/**
 * @brief __bt_put.
 * @param dbp B-Tree database handle.
 * @param key Key bytes.
 * @param data Value bytes.
 * @param flags Option flags controlling operation behavior.
 * @return Return code (RET_SUCCESS on success).
 */
int __bt_put(const DB* dbp, DBT* key, const DBT* data, u_int flags) {
    BTREE* t;
    DBT tkey, tdata;
    EPG* e = NULL;
    PAGE* h;
    indx_t index, nxtindex;
    pgno_t pg;
    size_t nbytes = 0;
    int dflags, exact;
    char *dest, db[NOVFLSIZE], kb[NOVFLSIZE];

    t = dbp->internal;

    /* Toss any page pinned across calls. */
    if (t->bt_pinned != NULL) {
        mpool_put(t->bt_mp, t->bt_pinned, 0);
        t->bt_pinned = NULL;
    }

    switch (flags) {
    case R_CURSOR:
        if (!ISSET(t, B_SEQINIT))
            goto einval;
        if (ISSET(t, B_DELCRSR))
            goto einval;
        break;
    case 0:
    case R_NOOVERWRITE:
        break;
    default:
    einval:
        errno = EINVAL;
        return (RET_ERROR);
    }

    if (ISSET(t, B_RDONLY)) {
        errno = EPERM;
        return (RET_ERROR);
    }

    /*
     * tiny-db policy: overflow pages are not supported.
     * Large values must be stored using chunking at a higher layer.
     */
    dflags = 0;
    if (key->size + data->size > t->bt_ovflsize) {
        (void)pg;
        (void)tkey;
        (void)tdata;
        (void)kb;
        (void)db;
        errno = E2BIG;
        return (RET_ERROR);
    }

    // Bytes needed for the leaf entry; used for fit checks and insertion.
    // Initialize early so code paths like R_CURSOR (goto delete) don't leave it unset.
    nbytes = NBLEAFDBT(key->size, data->size);

    /* Replace the cursor. */
    if (flags == R_CURSOR) {
        if ((h = mpool_get(t->bt_mp, t->bt_bcursor.pgno, 0)) == NULL)
            return (RET_ERROR);
        index = t->bt_bcursor.index;
        goto delete;
    }

    /*
     * Find the key to delete, or, the location at which to insert.  Bt_fast
     * and __bt_search pin the returned page.
     */
    if (t->bt_order == NOT || (e = bt_fast(t, key, data, &exact)) == NULL)
        if ((e = __bt_search_insert(t, key, nbytes, &exact)) == NULL)
            return (RET_ERROR);
    h = e->page;
    index = e->index;

    /*
     * Add the specified key/data pair to the tree.  If an identical key
     * is already in the tree, and R_NOOVERWRITE is set, an error is
     * returned.  If R_NOOVERWRITE is not set, the key is either added (if
     * duplicates are permitted) or an error is returned.
     *
     * Pages are split as required.
     */
    switch (flags) {
    case R_NOOVERWRITE:
        if (!exact)
            break;
        /*
         * One special case is if the cursor references the record and
         * it's been flagged for deletion.  Then, we delete the record,
         * leaving the cursor there -- this means that the inserted
         * record will not be seen in a cursor scan.
         */
        if (ISSET(t, B_DELCRSR) && t->bt_bcursor.pgno == h->pgno && t->bt_bcursor.index == index) {
            CLR(t, B_DELCRSR);
            goto delete;
        }
        mpool_put(t->bt_mp, h, 0);
        return (RET_SPECIAL);
    default:
        if (!exact || !ISSET(t, B_NODUPS))
            break;
        delete : if (__bt_dleaf(t, h, index) == RET_ERROR) {
            mpool_put(t->bt_mp, h, 0);
            return (RET_ERROR);
        }
        break;
    }

    /*
     * If not enough room, or the user has put a ceiling on the number of
     * keys permitted in the page, split the page.  The split code will
     * insert the key and data and unpin the current page.  If inserting
     * into the offset array, shift the pointers up.
     */
    if (__bt_would_split(h, nbytes)) {
        /* Should not happen: top-down search ensures leaf has room. */
        mpool_put(t->bt_mp, h, 0);
        errno = ENOMEM;
        return (RET_ERROR);
    }

    if (index < (nxtindex = NEXTINDEX(h)))
        memmove(h->linp + index + 1, h->linp + index, (nxtindex - index) * sizeof(indx_t));
    h->lower += sizeof(indx_t);

    h->linp[index] = h->upper -= nbytes;
    dest = (char*)h + h->upper;
    WR_BLEAF(dest, key, data, dflags);

    if (t->bt_order == NOT) {
        if (h->nextpg == P_INVALID) {
            if (index == NEXTINDEX(h) - 1) {
                t->bt_order = FORWARD;
                t->bt_last.index = index;
                t->bt_last.pgno = h->pgno;
            }
        } else if (h->prevpg == P_INVALID) {
            if (index == 0) {
                t->bt_order = BACK;
                t->bt_last.index = 0;
                t->bt_last.pgno = h->pgno;
            }
        }
    }

    // Save cursor target before mpool_put potentially invalidates the page pointer.
    const pgno_t inserted_pgno = h->pgno;
    const indx_t inserted_index = index;

    mpool_put(t->bt_mp, h, MPOOL_DIRTY);

    if (flags == R_SETCURSOR) {
        t->bt_bcursor.pgno = inserted_pgno;
        t->bt_bcursor.index = inserted_index;
    }
    SET(t, B_MODIFIED);
    return (RET_SUCCESS);
}

#ifdef STATISTICS
u_long bt_cache_hit, bt_cache_miss;
#endif

/*
 * BT_FAST -- Do a quick check for sorted data.
 *
 * Parameters:
 *	t:	tree
 *	key:	key to insert
 *
 * Returns:
 * 	EPG for new record or NULL if not found.
 */
/**
 * @brief bt_fast.
 * @param t B-Tree context.
 * @param key Key bytes.
 * @param data Value bytes.
 * @param exactp Output flag set on exact match.
 * @return Page/index reference, or NULL if not found.
 */
static EPG* bt_fast(BTREE* t, const DBT* key, const DBT* data, int* exactp) {
    PAGE* h;
    size_t nbytes;
    int cmp;

    if ((h = mpool_get(t->bt_mp, t->bt_last.pgno, 0)) == NULL) {
        t->bt_order = NOT;
        return (NULL);
    }
    t->bt_cur.page = h;
    t->bt_cur.index = t->bt_last.index;

    /*
     * If won't fit in this page or have too many keys in this page, have
     * to search to get split stack.
     */
    nbytes = NBLEAFDBT(key->size, data->size);
    if (__bt_would_split(h, nbytes))
        goto miss;

    if (t->bt_order == FORWARD) {
        if (t->bt_cur.page->nextpg != P_INVALID)
            goto miss;
        if (t->bt_cur.index != NEXTINDEX(h) - 1)
            goto miss;
        if ((cmp = __bt_cmp(t, key, &t->bt_cur)) < 0)
            goto miss;
        t->bt_last.index = cmp ? ++t->bt_cur.index : t->bt_cur.index;
    } else {
        if (t->bt_cur.page->prevpg != P_INVALID)
            goto miss;
        if (t->bt_cur.index != 0)
            goto miss;
        if ((cmp = __bt_cmp(t, key, &t->bt_cur)) > 0)
            goto miss;
        t->bt_last.index = 0;
    }
    *exactp = cmp == 0;
#ifdef STATISTICS
    ++bt_cache_hit;
#endif
    return (&t->bt_cur);

miss:
#ifdef STATISTICS
    ++bt_cache_miss;
#endif
    t->bt_order = NOT;
    mpool_put(t->bt_mp, h, 0);
    return (NULL);
}
