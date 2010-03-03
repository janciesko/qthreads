#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#ifdef HAVE_CPROPS
#include <cprops/linked_list.h>
#endif
#include <qthread/qthread.h>
#include <qthread/qloop.h>
#include <qthread/qlfqueue.h>
#include <qthread/qtimer.h>
#include "argparsing.h"

#define ELEMENT_COUNT 10000
#define THREAD_COUNT 128

#ifdef HAVE_CPROPS
static aligned_t cpqueuer(qthread_t * me, void *arg)
{
    cp_list *q = (cp_list *) arg;
    size_t i;

    for (i = 0; i < ELEMENT_COUNT; i++) {
	if (cp_list_append(q, (void *)me) == NULL) {
	    fprintf(stderr, "%i'th cp_list_append(q, %p) failed!\n", (int)i,
		    (void *)me);
	    perror("cp_list_append");
	    exit(-2);
	}
    }
    return 0;
}

static aligned_t cpdequeuer(qthread_t * me, void *arg)
{
    cp_list *q = (cp_list *) arg;
    size_t i;

    for (i = 0; i < ELEMENT_COUNT; i++) {
	while (cp_list_remove_head(q) == NULL) {
	    qthread_yield(me);
	}
    }
    return 0;
}
#endif

static aligned_t queuer(qthread_t * me, void *arg)
{
    qlfqueue_t *q = (qlfqueue_t *) arg;
    size_t i;

    for (i = 0; i < ELEMENT_COUNT; i++) {
	if (qlfqueue_enqueue(me, q, (void *)me) != QTHREAD_SUCCESS) {
	    fprintf(stderr, "qlfqueue_enqueue(q, %p) failed!\n", (void *)me);
	    exit(-2);
	}
    }
    return 0;
}

static aligned_t dequeuer(qthread_t * me, void *arg)
{
    qlfqueue_t *q = (qlfqueue_t *) arg;
    size_t i;

    for (i = 0; i < ELEMENT_COUNT; i++) {
	while (qlfqueue_dequeue(me, q) == NULL) {
	    qthread_yield(me);
	}
    }
    return 0;
}

#ifdef HAVE_CPROPS
void loop_cpqueuer(qthread_t * me, const size_t startat, const size_t stopat,
		   void *arg)
{
    size_t i;
    cp_list *q = (cp_list *) arg;

    for (i = startat; i < stopat; i++) {
	if (cp_list_append(q, (void *)me) == NULL) {
	    fprintf(stderr, "cp_list_append(q, %p) failed!\n", (void *)me);
	    exit(-2);
	}
    }
}

void loop_cpdequeuer(qthread_t * me, const size_t startat,
		     const size_t stopat, void *arg)
{
    size_t i;
    cp_list *q = (cp_list *) arg;

    for (i = startat; i < stopat; i++) {
	if (cp_list_remove_head(q) == NULL) {
	    fprintf(stderr, "cp_list_remove_head(q, %p) failed!\n",
		    (void *)me);
	    exit(-2);
	}
    }
}
#endif

static void loop_queuer(qthread_t * me, const size_t startat,
			const size_t stopat, void *arg)
{
    size_t i;
    qlfqueue_t *q = (qlfqueue_t *) arg;

    for (i = startat; i < stopat; i++) {
	if (qlfqueue_enqueue(me, q, (void *)me) != QTHREAD_SUCCESS) {
	    fprintf(stderr, "qlfqueue_enqueue(q, %p) failed!\n", (void *)me);
	    exit(-2);
	}
    }
}

static void loop_dequeuer(qthread_t * me, const size_t startat,
			  const size_t stopat, void *arg)
{
    size_t i;
    qlfqueue_t *q = (qlfqueue_t *) arg;

    for (i = startat; i < stopat; i++) {
	if (qlfqueue_dequeue(me, q) == NULL) {
	    fprintf(stderr, "qlfqueue_dequeue(q, %p) failed!\n", (void *)me);
	    exit(-2);
	}
    }
}

int main(int argc, char *argv[])
{
    qlfqueue_t *q;
    qthread_t *me;
    size_t i;
    aligned_t *rets;
    qtimer_t timer = qtimer_create();
#ifdef HAVE_CPROPS
    cp_list *cpq;
#endif

    assert(qthread_initialize() == QTHREAD_SUCCESS);
    me = qthread_self();

    CHECK_VERBOSE();

    if ((q = qlfqueue_create()) == NULL) {
	fprintf(stderr, "qlfqueue_create() failed!\n");
	exit(-1);
    }

    /* prime the pump */
    qt_loop_balance(0, THREAD_COUNT * ELEMENT_COUNT, loop_queuer, q);
    qt_loop_balance(0, THREAD_COUNT * ELEMENT_COUNT, loop_dequeuer, q);
    if (!qlfqueue_empty(q)) {
	fprintf(stderr, "qlfqueue not empty after priming!\n");
	exit(-2);
    }

    qtimer_start(timer);
    qt_loop_balance(0, THREAD_COUNT * ELEMENT_COUNT, loop_queuer, q);
    qtimer_stop(timer);
    printf("loop balance enqueue: %f secs\n", qtimer_secs(timer));
    qtimer_start(timer);
    qt_loop_balance(0, THREAD_COUNT * ELEMENT_COUNT, loop_dequeuer, q);
    qtimer_stop(timer);
    printf("loop balance dequeue: %f secs\n", qtimer_secs(timer));
    if (!qlfqueue_empty(q)) {
	fprintf(stderr, "qlfqueue not empty after loop balance test!\n");
	exit(-2);
    }
#ifdef HAVE_CPROPS
    cpq = cp_list_create();
    qtimer_start(timer);
    qt_loop_balance(0, THREAD_COUNT * ELEMENT_COUNT, loop_cpqueuer, cpq);
    qtimer_stop(timer);
    printf("loop balance cp enqueue: %f secs\n", qtimer_secs(timer));
    qtimer_start(timer);
    qt_loop_balance(0, THREAD_COUNT * ELEMENT_COUNT, loop_cpdequeuer, cpq);
    qtimer_stop(timer);
    printf("loop balance cp dequeue: %f secs\n", qtimer_secs(timer));
#endif

    rets = calloc(THREAD_COUNT, sizeof(aligned_t));
    assert(rets != NULL);
    qtimer_start(timer);
    for (i = 0; i < THREAD_COUNT; i++) {
	assert(qthread_fork(dequeuer, q, &(rets[i])) == QTHREAD_SUCCESS);
    }
    for (i = 0; i < THREAD_COUNT; i++) {
	assert(qthread_fork(queuer, q, NULL) == QTHREAD_SUCCESS);
    }
    for (i = 0; i < THREAD_COUNT; i++) {
	assert(qthread_readFF(me, NULL, &(rets[i])) == QTHREAD_SUCCESS);
    }
    qtimer_stop(timer);
    if (!qlfqueue_empty(q)) {
	fprintf(stderr, "qlfqueue not empty after threaded test!\n");
	exit(-2);
    }
    printf("threaded lf test: %f secs\n", qtimer_secs(timer));

#ifdef HAVE_CPROPS
    qtimer_start(timer);
    for (i = 0; i < THREAD_COUNT; i++) {
	assert(qthread_fork(cpdequeuer, cpq, &(rets[i])) == QTHREAD_SUCCESS);
    }
    for (i = 0; i < THREAD_COUNT; i++) {
	assert(qthread_fork(cpqueuer, cpq, NULL) == QTHREAD_SUCCESS);
    }
    for (i = 0; i < THREAD_COUNT; i++) {
	assert(qthread_readFF(me, NULL, &(rets[i])) == QTHREAD_SUCCESS);
    }
    qtimer_stop(timer);
    free(rets);
    printf("threaded cp test: %f secs\n", qtimer_secs(timer));

    cp_list_destroy(cpq);
#endif

    if (qlfqueue_destroy(me, q) != QTHREAD_SUCCESS) {
	fprintf(stderr, "qlfqueue_destroy() failed!\n");
	exit(-2);
    }

    iprintf("success!\n");

    return 0;
}
