/*
--------------------------------------------------------------------------------
This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.
This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.
You should have received a copy of the GNU Library General Public
License along with this library; if not, write to the
Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
Boston, MA  02110-1301, USA.
--------------------------------------------------------------------------------
*/

// Copyright (c) 2016-2021 John Seamons, ZL4VO/KF6VO

#include "types.h"
#include "config.h"
#include "kiwi.h"
#include "mem.h"
#include "misc.h"
#include "timer.h"
#include "web.h"
#include "coroutines.h"
#include "nbuf.h"

#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

#define ND_HIWAT 64
#define ND_LOWAT 32

#define NB_MAGIC 0xbabecafe

#define NBUF_MAGIC_B 0xbbbbbbbb
#define NBUF_MAGIC_E 0xbbbbeeee

#define NDESC_MAGIC_B 0xddddbbbb
#define NDESC_MAGIC_E 0xddddeeee

#ifdef NBUF_DEBUG
#define check_ndesc(nd)                                                                                      \
    {                                                                                                        \
        if (nd->magic_b != NDESC_MAGIC_B || nd->magic_e != NDESC_MAGIC_E) {                                  \
            lprintf("BAD NDESC MAGIC 0x%x 0x%x, %s line %d #############################################\n", \
                    nd->magic_b, nd->magic_e, __FILE__, __LINE__);                                           \
            dump_panic("check_ndesc");                                                                       \
        }                                                                                                    \
    }

#define check_nbuf(nb)                                                                                       \
    {                                                                                                        \
        if (nb->magic != NB_MAGIC || nb->magic_b != NBUF_MAGIC_B || nb->magic_e != NBUF_MAGIC_E) {           \
            lprintf("BAD NBUF MAGIC 0x%x 0x%x 0x%x, %s line %d #########################################\n", \
                    nb->magic, nb->magic_b, nb->magic_e, __FILE__, __LINE__);                                \
            dump_panic("check_nbuf");                                                                        \
        }                                                                                                    \
        if (nb->isFree) {                                                                                    \
            lprintf("BAD NBUF isFree, %s line %d #############################################\n",           \
                    __FILE__, __LINE__);                                                                     \
            dump_panic("check_nbuf");                                                                        \
        }                                                                                                    \
    }
#else
#define check_ndesc(nd)
#define check_nbuf(nb)
#endif

void nbuf_init() {
}

void nbuf_stat() {
}

void ndesc_init(ndesc_t* nd, struct mg_connection* mc) {
    memset(nd, 0, sizeof(ndesc_t));
#ifdef NBUF_DEBUG
    nd->magic_b = NDESC_MAGIC_B;
    nd->magic_e = NDESC_MAGIC_E;
#endif
    lock_init(&nd->lock);
    nd->mc = mc;
}

void ndesc_register(ndesc_t* nd) {
}

static nbuf_t* nbuf_malloc() {
    nbuf_t* nb;

    nb = (nbuf_t*)kiwi_malloc("nbuf", sizeof(nbuf_t));
#ifdef NBUF_DEBUG
    nb->magic = NB_MAGIC;
    nb->magic_b = NBUF_MAGIC_B;
    nb->magic_e = NBUF_MAGIC_E;
#endif
    return nb;
}

static void nbuf_free(nbuf_t* nb) {
    check_nbuf(nb);
#ifdef NBUF_DEBUG
    nb->magic = nb->magic_b = nb->magic_e = 0;
#endif
    nb->isFree = TRUE;
    kiwi_free("nbuf", nb);
}

static void nbuf_dumpq(ndesc_t* nd) {
    nbuf_t* bp;

    if (nd->dbug) printf("[");
    bp = nd->q;
    while (bp) {
        if (nd->dbug) printf("%d%s%s%d ", bp->id, bp->done ? "d" : ".", bp->dequeued ? "q" : ".", bp->ttl);
        bp = bp->next;
    }
    if (nd->dbug) printf("] ");
}

static bool nbuf_enqueue(ndesc_t* nd, nbuf_t* nb) {
    check_ndesc(nd);
    nbuf_t **q = &nd->q, **q_head = &nd->q_head;
    nbuf_t* dp;
    bool ovfl = FALSE;

    // collect done buffers (same thread where they're allocated)
    // decrement ttl counters
    if (nd->ttl) {
        dp = *q_head;
        while (dp) {
            check_nbuf(dp);
            if (dp->done) {
                assert(dp->ttl);
                dp->ttl--;
            }
            dp = dp->prev;
        }
    }

    while ((dp = *q_head) && (((nd->ttl == 0) && dp->done) || ((nd->ttl != 0) && dp->done && (dp->ttl == 0)))) {
        check_nbuf(dp);
        if (nd->dbug) printf("R%d ", dp->id);
        if (nd->dbug) nbuf_dumpq(nd);
        assert(dp->buf);
        kiwi_ifree(dp->buf, "nbuf:buf");
        *q_head = dp->prev;
        if (*q == dp) {
            *q = NULL;
            assert(*q_head == NULL);
        }
        else {
            (*q_head)->next = NULL;
        }
        nbuf_free(dp);
        if (nd->dbug) nbuf_dumpq(nd);
    }

    check_nbuf(nb);
    if (nd->ovfl && (nd->cnt < ND_LOWAT)) {
        nd->ovfl = FALSE;
    }

    if (nd->ovfl || (nd->cnt > ND_HIWAT)) {
        // if (!nd->ovfl && (nd->cnt > ND_HIWAT)) printf("HIWAT\n");
        nd->ovfl = TRUE;
        ovfl = TRUE;
    }
    else {
        nd->cnt++;
        check_nbuf(nb);
        if (*q) (*q)->prev = nb;
        nb->next = *q;
        *q = nb;
        nb->prev = NULL;
        if (!*q_head) *q_head = nb;
    }

    return ovfl;
}

void nbuf_allocq(ndesc_t* nd, char* s, int sl) {
    lock_holder holder(nd->lock);

    check_ndesc(nd);
    nbuf_t* nb;
    bool ovfl;
    static int id;

    assert(s != NULL);
    assert(sl > 0);
    nb = nbuf_malloc();
    // assert(nd->mc);
    nb->mc = nd->mc;
    // +1 so buffers which are strings can be null terminated after the fact
    // but don't reflect this extra byte in the nb->len count
    nb->buf = (char*)kiwi_imalloc("nbuf:buf", sl + 1);
    memcpy(nb->buf, s, sl);
    nb->len = sl;
    nb->done = FALSE;
    nb->dequeued = FALSE;
    nb->ttl = nd->ttl;
    if (nd->dbug) nb->id = id++;
    ovfl = nbuf_enqueue(nd, nb);
    if (nd->dbug) printf("A%d ", nb->id);
    if (nd->dbug) nbuf_dumpq(nd);

    check_nbuf(nb);
    if (ovfl) {
        kiwi_ifree(nb->buf, "nbuf:buf");
        nbuf_free(nb);
    }
}

nbuf_t* nbuf_dequeue(ndesc_t* nd) {
    lock_holder holder(nd->lock);

    check_ndesc(nd);
    nbuf_t** q_head = &nd->q_head;
    nbuf_t* nb;

    nb = *q_head;
    if (nb) check_nbuf(nb);
    while (nb && nb->dequeued) {
        nb = nb->prev;
        if (nb) check_nbuf(nb);
    }
    if (nb) {
        if (nd->dbug) printf("D%d ", nb->id);
        nb->dequeued = TRUE;
        nd->cnt--;
        if (nd->dbug) nbuf_dumpq(nd);
    }

    assert(!nb || (nb && !nb->done));
    // assert(!nb || ((nb->mc->remote_port == nd->mc->remote_port) && (strcmp(nb->mc->remote_ip, nd->mc->remote_ip) == 0)));
    if (nb) check_nbuf(nb);
    return nb;
}

int nbuf_queued(ndesc_t* nd) {
    lock_holder holder(nd->lock);

    check_ndesc(nd);
    nbuf_t* bp;
    int queued = 0;

    bp = nd->q;
    if (bp) check_nbuf(bp);
    while (bp) {
        if (!bp->dequeued) {
            queued++;
        }
        bp = bp->next;
        if (bp) check_nbuf(bp);
    }

    return queued;
}

void nbuf_cleanup(ndesc_t* nd) {
    lock_holder holder(nd->lock);
    check_ndesc(nd);
    nbuf_t **q = &nd->q, **q_head = &nd->q_head;
    nbuf_t* dp;
    int i = 0;

    while ((dp = *q_head) != NULL) {
        check_nbuf(dp);
        // assert(dp->buf);
        if (dp->buf == 0)
            lprintf("WARNING: dp->buf == NULL\n");
        else
            kiwi_ifree(dp->buf, "nbuf:buf");

        *q_head = dp->prev;
        if (dp == *q)
            *q = NULL;
        nbuf_free(dp);
        i++;
    }

    nd->cnt = 0;
    nd->ovfl = FALSE;

    // printf("nbuf_cleanup: removed %d, malloc %d\n", i, kiwi_malloc_stat());
}
