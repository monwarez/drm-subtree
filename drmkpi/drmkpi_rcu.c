/*-
 * Copyright (c) 2016 Matthew Macy (mmacy@mattmacy.io)
 * Copyright (c) 2017-2020 Hans Petter Selasky (hselasky@freebsd.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/kdb.h>

#include <ck_epoch.h>

#include <linux/kernel.h>

#include <drmkpi/rcupdate.h>
#include <drmkpi/srcu.h>

/*
 * By defining CONFIG_NO_RCU_SKIP DRMKPI RCU locks and asserts will
 * not be skipped during panic().
 */
#ifdef CONFIG_NO_RCU_SKIP
#define	RCU_SKIP(void) 0
#else
#define	RCU_SKIP(void)	unlikely(SCHEDULER_STOPPED() || kdb_active)
#endif

struct callback_head {
	STAILQ_ENTRY(callback_head) entry;
	rcu_callback_t func;
};

struct linux_epoch_head {
	STAILQ_HEAD(, callback_head) cb_head;
	struct mtx lock;
	struct task task;
} __aligned(CACHE_LINE_SIZE);

struct linux_epoch_record {
	ck_epoch_record_t epoch_record;
	TAILQ_HEAD(, task_struct) ts_head;
	int cpuid;
	int type;
} __aligned(CACHE_LINE_SIZE);

/*
 * Verify that "struct rcu_head" is big enough to hold "struct
 * callback_head". This has been done to avoid having to add special
 * compile flags for including ck_epoch.h to all clients of the
 * DRMKPI.
 */
CTASSERT(sizeof(struct rcu_head) == sizeof(struct callback_head));

/*
 * Verify that "epoch_record" is at beginning of "struct
 * linux_epoch_record":
 */
CTASSERT(offsetof(struct linux_epoch_record, epoch_record) == 0);

CTASSERT(TS_RCU_TYPE_MAX == RCU_TYPE_MAX);

static ck_epoch_t linux_epoch[RCU_TYPE_MAX];
static struct linux_epoch_head linux_epoch_head[RCU_TYPE_MAX];
DPCPU_DEFINE_STATIC(struct linux_epoch_record, linux_epoch_record[RCU_TYPE_MAX]);

static void linux_rcu_cleaner_func(void *, int);

static void
linux_rcu_runtime_init(void *arg __unused)
{
	struct linux_epoch_head *head;
	int i;
	int j;

	for (j = 0; j != RCU_TYPE_MAX; j++) {
		ck_epoch_init(&linux_epoch[j]);

		head = &linux_epoch_head[j];

		mtx_init(&head->lock, "LRCU-HEAD", NULL, MTX_DEF);
		TASK_INIT(&head->task, 0, linux_rcu_cleaner_func, head);
		STAILQ_INIT(&head->cb_head);

		CPU_FOREACH(i) {
			struct linux_epoch_record *record;

			record = &DPCPU_ID_GET(i, linux_epoch_record[j]);

			record->cpuid = i;
			record->type = j;
			ck_epoch_register(&linux_epoch[j],
			    &record->epoch_record, NULL);
			TAILQ_INIT(&record->ts_head);
		}
	}
}
SYSINIT(linux_rcu_runtime, SI_SUB_CPU, SI_ORDER_ANY, linux_rcu_runtime_init, NULL);

static void
linux_rcu_runtime_uninit(void *arg __unused)
{
	struct linux_epoch_head *head;
	int j;

	for (j = 0; j != RCU_TYPE_MAX; j++) {
		head = &linux_epoch_head[j];

		mtx_destroy(&head->lock);
	}
}
SYSUNINIT(linux_rcu_runtime, SI_SUB_LOCK, SI_ORDER_SECOND, linux_rcu_runtime_uninit, NULL);

static void
linux_rcu_cleaner_func(void *context, int pending __unused)
{
	struct linux_epoch_head *head;
	struct callback_head *rcu;
	STAILQ_HEAD(, callback_head) tmp_head;
	uintptr_t offset;

	linux_set_current(curthread);

	head = context;

	/* move current callbacks into own queue */
	mtx_lock(&head->lock);
	STAILQ_INIT(&tmp_head);
	STAILQ_CONCAT(&tmp_head, &head->cb_head);
	mtx_unlock(&head->lock);

	/* synchronize */
	drmkpi_synchronize_rcu(head - linux_epoch_head);

	/* dispatch all callbacks, if any */
	while ((rcu = STAILQ_FIRST(&tmp_head)) != NULL) {
		STAILQ_REMOVE_HEAD(&tmp_head, entry);

		offset = (uintptr_t)rcu->func;

		if (offset < LINUX_KFREE_RCU_OFFSET_MAX)
			kfree((char *)rcu - offset);
		else
			rcu->func((struct rcu_head *)rcu);
	}
}

void
drmkpi_rcu_read_lock(unsigned type)
{
	struct linux_epoch_record *record;
	struct task_struct *ts;

	MPASS(type < RCU_TYPE_MAX);

	if (RCU_SKIP())
		return;

	/*
	 * Pin thread to current CPU so that the unlock code gets the
	 * same per-CPU epoch record:
	 */
	sched_pin();

	record = &DPCPU_GET(linux_epoch_record[type]);
	ts = current;

	/*
	 * Use a critical section to prevent recursion inside
	 * ck_epoch_begin(). Else this function supports recursion.
	 */
	critical_enter();
	ck_epoch_begin(&record->epoch_record, NULL);
	ts->rcu_recurse[type]++;
	if (ts->rcu_recurse[type] == 1)
		TAILQ_INSERT_TAIL(&record->ts_head, ts, rcu_entry[type]);
	critical_exit();
}

void
drmkpi_rcu_read_unlock(unsigned type)
{
	struct linux_epoch_record *record;
	struct task_struct *ts;

	MPASS(type < RCU_TYPE_MAX);

	if (RCU_SKIP())
		return;

	record = &DPCPU_GET(linux_epoch_record[type]);
	ts = current;

	/*
	 * Use a critical section to prevent recursion inside
	 * ck_epoch_end(). Else this function supports recursion.
	 */
	critical_enter();
	ck_epoch_end(&record->epoch_record, NULL);
	ts->rcu_recurse[type]--;
	if (ts->rcu_recurse[type] == 0)
		TAILQ_REMOVE(&record->ts_head, ts, rcu_entry[type]);
	critical_exit();

	sched_unpin();
}

static void
linux_synchronize_rcu_cb(ck_epoch_t *epoch __unused, ck_epoch_record_t *epoch_record, void *arg __unused)
{
	struct linux_epoch_record *record =
	    container_of(epoch_record, struct linux_epoch_record, epoch_record);
	struct thread *td = curthread;
	struct task_struct *ts;

	/* check if blocked on the current CPU */
	if (record->cpuid == PCPU_GET(cpuid)) {
		bool is_sleeping = 0;
		u_char prio = 0;

		/*
		 * Find the lowest priority or sleeping thread which
		 * is blocking synchronization on this CPU core. All
		 * the threads in the queue are CPU-pinned and cannot
		 * go anywhere while the current thread is locked.
		 */
		TAILQ_FOREACH(ts, &record->ts_head, rcu_entry[record->type]) {
			if (ts->task_thread->td_priority > prio)
				prio = ts->task_thread->td_priority;
			is_sleeping |= (ts->task_thread->td_inhibitors != 0);
		}

		if (is_sleeping) {
			thread_unlock(td);
			pause("W", 1);
			thread_lock(td);
		} else {
			/* set new thread priority */
			sched_prio(td, prio);
			/* task switch */
			mi_switch(SW_VOL | SWT_RELINQUISH);
			/*
			 * It is important the thread lock is dropped
			 * while yielding to allow other threads to
			 * acquire the lock pointed to by
			 * TDQ_LOCKPTR(td). Currently mi_switch() will
			 * unlock the thread lock before
			 * returning. Else a deadlock like situation
			 * might happen.
			 */
			thread_lock(td);
		}
	} else {
		/*
		 * To avoid spinning move execution to the other CPU
		 * which is blocking synchronization. Set highest
		 * thread priority so that code gets run. The thread
		 * priority will be restored later.
		 */
		sched_prio(td, 0);
		sched_bind(td, record->cpuid);
	}
}

void
drmkpi_synchronize_rcu(unsigned type)
{
	struct thread *td;
	int was_bound;
	int old_cpu;
	int old_pinned;
	u_char old_prio;

	MPASS(type < RCU_TYPE_MAX);

	if (RCU_SKIP())
		return;

	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
	    "drmkpi_synchronize_rcu() can sleep");

	td = curthread;
	DROP_GIANT();

	/*
	 * Synchronizing RCU might change the CPU core this function
	 * is running on. Save current values:
	 */
	thread_lock(td);

	old_cpu = PCPU_GET(cpuid);
	old_pinned = td->td_pinned;
	old_prio = td->td_priority;
	was_bound = sched_is_bound(td);
	sched_unbind(td);
	td->td_pinned = 0;
	sched_bind(td, old_cpu);

	ck_epoch_synchronize_wait(&linux_epoch[type],
	    &linux_synchronize_rcu_cb, NULL);

	/* restore CPU binding, if any */
	if (was_bound != 0) {
		sched_bind(td, old_cpu);
	} else {
		/* get thread back to initial CPU, if any */
		if (old_pinned != 0)
			sched_bind(td, old_cpu);
		sched_unbind(td);
	}
	/* restore pinned after bind */
	td->td_pinned = old_pinned;

	/* restore thread priority */
	sched_prio(td, old_prio);
	thread_unlock(td);

	PICKUP_GIANT();
}

void
drmkpi_rcu_barrier(unsigned type)
{
	struct linux_epoch_head *head;

	MPASS(type < RCU_TYPE_MAX);

	drmkpi_synchronize_rcu(type);

	head = &linux_epoch_head[type];

	/* wait for callbacks to complete */
	taskqueue_drain(taskqueue_fast, &head->task);
}

void
drmkpi_call_rcu(unsigned type, struct rcu_head *context, rcu_callback_t func)
{
	struct callback_head *rcu;
	struct linux_epoch_head *head;

	MPASS(type < RCU_TYPE_MAX);

	rcu = (struct callback_head *)context;
	head = &linux_epoch_head[type];

	mtx_lock(&head->lock);
	rcu->func = func;
	STAILQ_INSERT_TAIL(&head->cb_head, rcu, entry);
	taskqueue_enqueue(taskqueue_fast, &head->task);
	mtx_unlock(&head->lock);
}

int
drmkpi_init_srcu_struct(struct srcu_struct *srcu)
{
	return (0);
}

void
drmkpi_cleanup_srcu_struct(struct srcu_struct *srcu)
{
}

int
drmkpi_srcu_read_lock(struct srcu_struct *srcu)
{
	drmkpi_rcu_read_lock(RCU_TYPE_SLEEPABLE);
	return (0);
}

void
drmkpi_srcu_read_unlock(struct srcu_struct *srcu, int key __unused)
{
	drmkpi_rcu_read_unlock(RCU_TYPE_SLEEPABLE);
}

void
drmkpi_synchronize_srcu(struct srcu_struct *srcu)
{
	drmkpi_synchronize_rcu(RCU_TYPE_SLEEPABLE);
}

void
drmkpi_srcu_barrier(struct srcu_struct *srcu)
{
	drmkpi_rcu_barrier(RCU_TYPE_SLEEPABLE);
}
