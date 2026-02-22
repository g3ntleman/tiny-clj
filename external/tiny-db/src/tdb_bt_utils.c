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
static char sccsid[] = "@(#)bt_utils.c	8.4 (Berkeley) 2/21/94";
#endif /* LIBC_SCCS and not lint */

#include <sys/param.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tdb_bsd_db.h"
#include "tdb_bsd_btree.h"

/*
 * __BT_RET -- Build return key/data pair as a result of search or scan.
 *
 * Parameters:
 *	t:	tree
 *	d:	LEAF to be returned to the user.
 *	key:	user's key structure (NULL if not to be filled in)
 *	data:	user's data structure
 *
 * Returns:
 *	RET_SUCCESS, RET_ERROR.
 */
/**
 * @brief __bt_ret.
 * @param t B-Tree context.
 * @param e Page/index search state.
 * @param key Key bytes.
 * @param data Value bytes.
 * @return Return code (RET_SUCCESS on success).
 */
int __bt_ret(BTREE* t, EPG* e, DBT* key, DBT* data) {
    register BLEAF* bl;
    register void* p;

    bl = GETBLEAF(e->page, e->index);

    /*
     * tiny-db policy: overflow pages are not supported.
     * If we ever encounter big keys/data flags, treat this as corruption
     * (or an old on-flash format) and error out.
     */
    if (bl->flags & P_BIGDATA) {
        errno = EFTYPE;
        return (RET_ERROR);
    } else if (ISSET(t, B_DB_LOCK)) {
        /* Use +1 in case the first record retrieved is 0 length. */
        if (bl->dsize + 1 > t->bt_dbufsz) {
            if ((p = (void*)realloc(t->bt_dbuf, bl->dsize + 1)) == NULL)
                return (RET_ERROR);
            t->bt_dbuf = p;
            t->bt_dbufsz = bl->dsize + 1;
        }
        memmove(t->bt_dbuf, bl->bytes + bl->ksize, bl->dsize);
        data->size = bl->dsize;
        data->data = t->bt_dbuf;
    } else {
        data->size = bl->dsize;
        data->data = bl->bytes + bl->ksize;
    }

    if (key == NULL)
        return (RET_SUCCESS);

    if (bl->flags & P_BIGKEY) {
        errno = EFTYPE;
        return (RET_ERROR);
    } else if (ISSET(t, B_DB_LOCK)) {
        if (bl->ksize > t->bt_kbufsz) {
            if ((p = (void*)realloc(t->bt_kbuf, bl->ksize)) == NULL)
                return (RET_ERROR);
            t->bt_kbuf = p;
            t->bt_kbufsz = bl->ksize;
        }
        memmove(t->bt_kbuf, bl->bytes, bl->ksize);
        key->size = bl->ksize;
        key->data = t->bt_kbuf;
    } else {
        key->size = bl->ksize;
        key->data = bl->bytes;
    }
    return (RET_SUCCESS);
}

/*
 * __BT_CMP -- Compare a key to a given record.
 *
 * Parameters:
 *	t:	tree
 *	k1:	DBT pointer of first arg to comparison
 *	e:	pointer to EPG for comparison
 *
 * Returns:
 *	< 0 if k1 is < record
 *	= 0 if k1 is = record
 *	> 0 if k1 is > record
 */
/**
 * @brief __bt_cmp.
 * @param t B-Tree context.
 * @param k1 Key bytes.
 * @param e Page/index search state.
 * @return Comparison result (<0, 0, >0).
 */
int __bt_cmp(BTREE* t, const DBT* k1, EPG* e) {
    BINTERNAL* bi;
    BLEAF* bl;
    DBT k2;
    PAGE* h;
    void* bigkey;

    /*
     * The left-most key on internal pages, at any level of the tree, is
     * guaranteed by the following code to be less than any user key.
     * This saves us from having to update the leftmost key on an internal
     * page when the user inserts a new key in the tree smaller than
     * anything we've yet seen.
     */
    h = e->page;
    if (e->index == 0 && h->prevpg == P_INVALID && !(h->flags & P_BLEAF))
        return (1);

    bigkey = NULL;
    if (h->flags & P_BLEAF) {
        bl = GETBLEAF(h, e->index);
        if (bl->flags & P_BIGKEY) {
            errno = EFTYPE;
            return (RET_ERROR);
        } else {
            k2.data = bl->bytes;
            k2.size = bl->ksize;
        }
    } else {
        bi = GETBINTERNAL(h, e->index);
        if (bi->flags & P_BIGKEY) {
            errno = EFTYPE;
            return (RET_ERROR);
        } else {
            k2.data = bi->bytes;
            k2.size = bi->ksize;
        }
    }

    (void)bigkey;
    return ((*t->bt_cmp)(k1, &k2));
}

/*
 * __BT_DEFCMP -- Default comparison routine.
 *
 * Parameters:
 *	a:	DBT #1
 *	b: 	DBT #2
 *
 * Returns:
 *	< 0 if a is < b
 *	= 0 if a is = b
 *	> 0 if a is > b
 */
/**
 * @brief __bt_defcmp.
 * @param a Input pointer.
 * @param b Input pointer.
 * @return Comparison result (<0, 0, >0).
 */
int __bt_defcmp(const DBT* a, const DBT* b) {
    size_t len;
    register u_char *p1, *p2;

    /*
     * XXX
     * If a size_t doesn't fit in an int, this routine can lose.
     * What we need is a integral type which is guaranteed to be
     * larger than a size_t, and there is no such thing.
     */
    len = MIN(a->size, b->size);
    for (p1 = a->data, p2 = b->data; len--; ++p1, ++p2)
        if (*p1 != *p2)
            return ((int)*p1 - (int)*p2);
    return ((int)a->size - (int)b->size);
}

/*
 * __BT_DEFPFX -- Default prefix routine.
 *
 * Parameters:
 *	a:	DBT #1
 *	b: 	DBT #2
 *
 * Returns:
 *	Number of bytes needed to distinguish b from a.
 */
/**
 * @brief __bt_defpfx.
 * @param a Input pointer.
 * @param b Input pointer.
 * @return Computed size value.
 */
size_t __bt_defpfx(const DBT* a, const DBT* b) {
    register u_char *p1, *p2;
    register size_t cnt, len;

    cnt = 1;
    len = MIN(a->size, b->size);
    for (p1 = a->data, p2 = b->data; len--; ++p1, ++p2, ++cnt)
        if (*p1 != *p2)
            return (cnt);

    /* a->size must be <= b->size, or they wouldn't be in this order. */
    return (a->size < b->size ? a->size + 1 : a->size);
}
