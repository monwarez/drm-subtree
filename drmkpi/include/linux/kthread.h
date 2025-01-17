/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2017 Mellanox Technologies, Ltd.
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
 *
 * $FreeBSD$
 */

#ifndef	_LINUX_KTHREAD_H_
#define	_LINUX_KTHREAD_H_

#include <linux/sched.h>

#include <sys/unistd.h>
#include <sys/kthread.h>

struct kthr_wrap {
	int (*func)(void *);
	void *arg;
};

#define	kthread_run(fn, data, fmt, ...)	({				\
	struct thread *__td;						\
	struct kthr_wrap *w;						\
									\
	w = malloc(sizeof(struct kthr_wrap), M_DRMKMALLOC, 0);		\
	w->func = fn;							\
	w->arg = data;							\
									\
	if (kthread_add(drmkpi_kthread_fn, w, NULL, &__td,		\
	    RFSTOPPED, 0, fmt, ## __VA_ARGS__)) {			\
		__td = NULL;						\
		free(w, M_DRMKMALLOC);					\
	} else								\
		__td = drmkpi_kthread_setup_and_run(__td);		\
	__td;								\
})

int drmkpi_kthread_stop(struct thread *);
bool drmkpi_kthread_should_stop_task(struct thread *td);
bool drmkpi_kthread_should_stop(void);
int drmkpi_kthread_park(struct thread *);
void drmkpi_kthread_parkme(void);
bool drmkpi_kthread_should_park(void);
void drmkpi_kthread_unpark(struct thread *);
void drmkpi_kthread_fn(void *);
struct thread *drmkpi_kthread_setup_and_run(struct thread *);

#define	kthread_stop(task)		drmkpi_kthread_stop(task)
#define	kthread_should_stop()		drmkpi_kthread_should_stop()
#define	kthread_should_stop_task(task)	drmkpi_kthread_should_stop_task(task)
#define	kthread_park(task)		drmkpi_kthread_park(task)
#define	kthread_parkme()		drmkpi_kthread_parkme()
#define	kthread_should_park()		drmkpi_kthread_should_park()
#define	kthread_unpark(task)		drmkpi_kthread_unpark(task)

#endif /* _LINUX_KTHREAD_H_ */
