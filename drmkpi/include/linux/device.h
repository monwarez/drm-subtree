/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2016 Mellanox Technologies, Ltd.
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

#ifndef __DRMKPI_LINUX_DEVICE_H__
#define	__DRMKPI_LINUX_DEVICE_H__

#include <linux/err.h>
#include <linux/types.h>
#include <linux/module.h>

#include <sys/bus.h>

struct device_type {
	const char	*name;
};
#define	device _device

char *kvasprintf(gfp_t, const char *, va_list);
char *kasprintf(gfp_t, const char *, ...);

#define dev_name(dev, ...) device_get_name(dev)
#define dev_dbg(dev, ...) device_printf(dev, ##__VA_ARGS__)
#define dev_err(dev, ...) device_printf(dev, ##__VA_ARGS__)
#define dev_warn(dev, ...) device_printf(dev, ##__VA_ARGS__)
#define dev_info(dev, ...) device_printf(dev, ##__VA_ARGS__)
#define dev_printk(level, dev, ...) device_printf(dev, ##__VA_ARGS__)

#define	device_is_registered(dev)	0
#define	put_device(dev)
#define	get_device(dev)	dev

#define	devm_add_action(parent, func, dev)	0

#endif	/* __DRMKPI_LINUX__DEVICE_H__ */
