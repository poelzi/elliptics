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
#include <sys/time.h>
#include <sys/syscall.h>

#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <netinet/in.h>

#include "dnet/packet.h"
#include "dnet/interface.h"

#include "hash.h"
#include "common.h"

#ifndef __unused
#define __unused	__attribute__ ((unused))
#endif

static int stat_mem, stat_la, stat_fs;

static int stat_complete(struct dnet_net_state *state,
			struct dnet_cmd *cmd,
			struct dnet_attr *attr,
			void *priv)
{
	float la[3];
	struct dnet_stat *st;
	char str[64];
	struct tm tm;
	struct timeval tv;
	FILE *stream = priv;

	if (!state || !cmd || !attr)
		return 0;

	if (attr->size != sizeof(struct dnet_stat))
		return 0;

	if (!stat_mem && !stat_la && !stat_fs)
		return 0;

	gettimeofday(&tv, NULL);
	localtime_r((time_t *)&tv.tv_sec, &tm);
	strftime(str, sizeof(str), "%F %R:%S", &tm);

	fprintf(stream, "%s.%06lu :", str, (unsigned long)tv.tv_usec);

	st = (struct dnet_stat *)(attr + 1);

	dnet_convert_stat(st);

	la[0] = (float)st->la[0] / 100.0;
	la[1] = (float)st->la[1] / 100.0;
	la[2] = (float)st->la[2] / 100.0;


	fprintf(stream, "%s: %s: ", dnet_dump_id(cmd->id), dnet_state_dump_addr(state));

	if (stat_la)
		fprintf(stream, "la: %3.2f %3.2f %3.2f ", la[0], la[1], la[2]);

	if (stat_mem)
		fprintf(stream, "mem: total: %8llu kB, free: %8llu kB, cache: %8llu kB, buffers: %8llu, active: %8llu, inactive: %8llu ",
			(unsigned long long)st->vm_total, (unsigned long long)st->vm_free,
			(unsigned long long)st->vm_cached, (unsigned long long)st->vm_buffers,
			(unsigned long long)st->vm_active, (unsigned long long)st->vm_inactive);

	if (stat_fs)
		fprintf(stream, "fs: total: %8llu mB, avail: %8llu/%8llu mB ",
			(unsigned long long)(st->frsize * st->blocks / 1024 / 1024),
			(unsigned long long)(st->bavail * st->bsize / 1024 / 1024),
			(unsigned long long)(st->bfree * st->bsize / 1024 / 1024));

	fprintf(stream, "\n");
	fflush(stream);

	return 0;
}

static void stat_usage(char *p)
{
	fprintf(stderr, "Usage: %s\n"
			" -a addr:port:family  - creates a node with given network address\n"
			" -r addr:port:family  - adds a route to the given node\n"
			" -l log               - log file. Default: disabled\n"
			" -L log               - statistics log. Default: stdout\n"
			" -w timeout           - wait timeout in seconds used to wait for content sync.\n"
			" -m mask              - log events mask\n"
			" -i id                - node's ID (zero by default)\n"
			" -I id                - request statistics from node which handles given id\n"
			" -t timeout           - timeout in seconds to repeatedly request statistics\n"
			" -M                   - show memory usage statistics\n"
			" -F                   - show filesystem usage statistics\n"
			" -A                   - show load average statistics\n"
	       , p);
}

int main(int argc, char *argv[])
{
	int ch, err, i, have_remote = 0;
	struct dnet_node *n = NULL;
	struct dnet_config cfg, rem;
	int max_id_idx = 1000, id_idx = 0;
	int timeout;
	unsigned char id[max_id_idx][DNET_ID_SIZE];
	char *logfile = NULL, *statfile = "/dev/stdout";
	FILE *log = NULL, *stat;

	memset(&cfg, 0, sizeof(struct dnet_config));

	cfg.sock_type = SOCK_STREAM;
	cfg.proto = IPPROTO_TCP;
	cfg.wait_timeout = 60*60;
	cfg.log_mask = ~0;

	timeout = 1;

	memcpy(&rem, &cfg, sizeof(struct dnet_config));

	while ((ch = getopt(argc, argv, "MFAt:m:w:l:I:i:a:r:h")) != -1) {
		switch (ch) {
			case 'M':
				stat_mem = 1;
				break;
			case 'F':
				stat_fs = 1;
				break;
			case 'A':
				stat_la = 1;
				break;
			case 't':
				timeout = atoi(optarg);
				break;
			case 'm':
				cfg.log_mask = strtoul(optarg, NULL, 0);
				break;
			case 'w':
				cfg.wait_timeout = atoi(optarg);
				break;
			case 'L':
				statfile = optarg;
				break;
			case 'l':
				logfile = optarg;
				break;
			case 'I':
				if (id_idx < max_id_idx) {
					err = dnet_parse_numeric_id(optarg, id[id_idx]);
					if (err)
						return err;
					id_idx++;
				}
				break;
			case 'i':
				err = dnet_parse_numeric_id(optarg, cfg.id);
				if (err)
					return err;
				break;
			case 'a':
				err = dnet_parse_addr(optarg, &cfg);
				if (err)
					return err;
				break;
			case 'r':
				err = dnet_parse_addr(optarg, &rem);
				if (err)
					return err;
				have_remote = 1;
				break;
			case 'h':
			default:
				stat_usage(argv[0]);
				return -1;
		}
	}

	if (!have_remote) {
		fprintf(stderr, "No remote node specified to route requests.\n");
		return -ENOENT;
	}

	if (!logfile)
		fprintf(stderr, "No log file found, logging will be disabled.\n");

	if (logfile) {
		log = fopen(logfile, "a");
		if (!log) {
			err = -errno;
			fprintf(stderr, "Failed to open log file %s: %s.\n", logfile, strerror(errno));
			return err;
		}

		cfg.log_private = log;
		cfg.log = dnet_common_log;
	}

	stat = fopen(statfile, "a");
	if (!stat) {
		err = -errno;
		fprintf(stderr, "Failed to open stat file %s: %s.\n", statfile, strerror(errno));
		return err;
	}

	n = dnet_node_create(&cfg);
	if (!n)
		return -1;

	err = dnet_add_state(n, &rem);
	if (err)
		return err;

	while (1) {
		if (!id_idx) {
			err = dnet_request_stat(n, NULL, stat_complete, stat);
			if (err < 0)
				return err;
		}

		for (i=0; i<id_idx; ++i) {
			err = dnet_request_stat(n, id[i], stat_complete, stat);
			if (err < 0)
				return err;
		}

		sleep(timeout);
	}

	return 0;
}
