#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include "qthread_asserts.h"
#include "qt_atomics.h"
#include "qt_mpool.h"
#include <qthread/feb_barrier.h>
#include <qthread/qthread.h>
#include "qthread_innards.h"

struct qt_feb_barrier_s
{
    syncvar_t in_gate;
    syncvar_t out_gate;
    aligned_t blockers;
    size_t max_blockers;
};

static qt_mpool QTHREAD_CASLOCK(feb_barrier_pool);
static Q_UNUSED qt_feb_barrier_t *global_barrier = NULL;

#ifndef UNPOOLED
static void cleanup_feb_barrier(void)
{
    if (feb_barrier_pool) {
	qt_mpool_destroy(feb_barrier_pool);
    }
    feb_barrier_pool = NULL;
}
#endif

void qt_feb_barrier_internal_init(void)
{
    QTHREAD_CASLOCK_INIT(feb_barrier_pool, NULL);
}

qt_feb_barrier_t *qt_feb_barrier_create(qthread_t *me, size_t max_threads)
{
    qt_feb_barrier_t *b;
#ifndef UNPOOLED
    if (feb_barrier_pool == NULL) {
	qt_mpool bp = qt_mpool_create(qthread_num_shepherds()>1, sizeof(struct qt_feb_barrier_s), -1);
	if (QT_CAS(feb_barrier_pool, NULL, bp) != NULL) {
	    /* someone else created an mpool first */
	    qt_mpool_destroy(bp);
	} else {
	    qthread_internal_cleanup(cleanup_feb_barrier);
	}
    }
    b = qt_mpool_alloc(feb_barrier_pool);
#else
    b = malloc(sizeof(struct qt_feb_barrier_s));
#endif
    b->blockers = 0;
    b->max_blockers = max_threads;
    b->in_gate.u.w = 0;
    b->out_gate.u.w = 0;
    qthread_syncvar_fill(me, &b->in_gate);
    qthread_syncvar_empty(me, &b->out_gate);
    return b;
}

void qt_feb_barrier_enter(qthread_t *me, qt_feb_barrier_t *b)
{
    aligned_t waiters;
    qassert_retvoid(b);
    /* pass through the in_gate */
    qthread_syncvar_readFF(me, NULL, &b->in_gate);
    /* increment the blocker count */
    waiters = qthread_incr(&b->blockers, 1) + 1;
    if (waiters == b->max_blockers) {
	/* last guy into the barrier, close the in_gate, open the out_gate */
	qthread_syncvar_empty(me, &b->in_gate);
	qthread_syncvar_fill(me, &b->out_gate);
    } else {
	/* pass through the out_gate */
	qthread_syncvar_readFF(me, NULL, &b->out_gate);
    }
    /* I'm on the way out, so decrement the blocker count */
    waiters = qthread_incr(&b->blockers, -1) - 1;
    if (waiters == 0) {
	/* last guy out of the barrier, close the out_gate, open the in_gate */
	qthread_syncvar_empty(me, &b->out_gate);
	qthread_syncvar_fill(me, &b->in_gate);
    }
}

void qt_feb_barrier_destroy(qthread_t *me, qt_feb_barrier_t *b)
{
    assert(feb_barrier_pool != NULL);
    assert(b->blockers == 0);
    qthread_syncvar_fill(me, &b->out_gate);
    qthread_syncvar_fill(me, &b->in_gate);
#ifndef UNPOOLED
    qt_mpool_free(feb_barrier_pool, b);
#else
    free(b);
#endif
}

#ifdef QTHREAD_GLOBAL_FEB_BARRIER
void qthread_reset_forCount(qthread_t *);	// KBW
void qt_global_barrier(qthread_t *me)
{
    assert(global_barrier);
    qt_feb_barrier_enter(me, global_barrier);
    //  now execute code on one thread that everyone needs to see -- should be
    //     at middle of barrier but does not seem to work there -- so here with double barrier
    //     blech.  akp -2/9/10
    qthread_reset_forCount(qthread_self());	// for loop reset on each thread
    qt_feb_barrier_enter(me, global_barrier);
    return;
}

// allow barrer initization from C
void qt_global_barrier_init(int size, int debug)
{
    if (global_barrier == NULL) {
	global_barrier = qt_feb_barrier_create(NULL, size);
	assert(global_barrier);
    }
}

void qt_global_barrier_destroy()
{
    if (global_barrier) {
	qt_feb_barrier_destroy(qthread_self(), global_barrier);
	global_barrier = NULL;
    }
}
#endif
