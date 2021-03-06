/*
 * 2008+ Copyright (c) Evgeniy Polyakov <zbr@ioremap.net>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "elliptics.h"

#ifdef HAVE_SENDFILE4_SUPPORT
#include <sys/sendfile.h>
int dnet_sendfile(struct dnet_net_state *st, int fd, uint64_t *offset, uint64_t size)
{
	int err;

	err = sendfile(st->s, fd, (off_t *)offset, size);
	if (err < 0)
		return -errno;

	return err;
}
#elif HAVE_SENDFILE7_SUPPORT
#include <sys/uio.h>
int dnet_sendfile(struct dnet_net_state *st, int fd, uint64_t *offset, uint64_t size)
{
	int err;

	err = sendfile(fd, st->s, *offset, size, NULL, &size, 0);
	if (err && errno != EAGAIN)
		return -errno;

	if (size) {
		*offset += size;
		return size;
	}

	return -EAGAIN;
}
#elif HAVE_SENDFILE6_SUPPORT
#include <sys/uio.h>
int dnet_sendfile(struct dnet_net_state *st, int fd, uint64_t *offset, uint64_t size)
{
	int err;

	err = sendfile(fd, st->s, *offset, &size, NULL, 0);
	if (err && errno != EAGAIN)
		return -errno;

	if (size) {
		*offset += size;
		return size;
	}

	return -EAGAIN;
}
#else
int dnet_sendfile(struct dnet_net_state *st, int fd, uint64_t *offset, uint64_t size)
{
	char buf[4096];
	suint64_t err;
	uint64_t total = 0;

	err = lseek(fd, *offset, SEEK_SET);
	if (err < 0) {
		err = -errno;
		dnet_log_err(st->n, "failed to seek to %llu",
				(unsigned long long)*offset);
		return err;
	}

	while (size) {
		uint64_t sz = size;

		if (sz > sizeof(buf))
			sz = sizeof(buf);

		err = read(fd, buf, sz);
		if (err < 0) {
			if (errno == EAGAIN || errno == EINTR)
				break;
			err = -errno;
			dnet_log_err(st->n, "failed to read %zu bytes at %llu",
					sz, (unsigned long long)*offset);
			return err;
		}

		if (!err)
			break;

		sz = err;

		while (sz) {
			err = send(st->s, buf, sz, 0);
			if (err < 0) {
				if (errno == EAGAIN || errno == EINTR)
					break;
				return err;
			}
			if (!err)
				return -ECONNRESET;

			*offset += err;
			size -= err;
			total += err;
			sz -= err;
			err = 0;
		}

		if (err)
			break;
	}

	if (total)
		return total;

	return -EAGAIN;
}
#endif
