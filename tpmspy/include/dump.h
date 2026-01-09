#pragma once
#ifndef DUMP_H
#define DUMP_H

#include <stddef.h>
#include <stdint.h>

#include "defs.h"

#define DUMP_MAGIC { 'T', 'P', 'M', 'S', 'P', 'Y' }
#define DUMP_VERSION_MAJOR 1
#define DUMP_VERSION_MINOR 0

#define DUMP_HEADER \
	(struct dump_header){ \
		.magic = DUMP_MAGIC, \
		.version = { DUMP_VERSION_MAJOR, DUMP_VERSION_MINOR } \
	}

struct packed dump_header {
	uint8_t magic[6];
	uint8_t version[2];
};

struct dump_packet_endpoint {
	int32_t type; /* enum socket_type */
	int32_t fd;   /* int (fd) */
};

typedef int32_t dump_fd_t;

struct packed dump_time {
	int64_t s;
	int32_t us;
};

struct packed dump_packet {
	struct dump_packet_endpoint dp_src, dp_dst;
	struct dump_time dp_time;

	uint32_t dp_fds;
	uint32_t dp_data;
	char bytes[];
};

void dump_packet_marshall(struct dump_packet *dst, const struct dump_packet *src);
void dump_packet_marshall_inplace(struct dump_packet *p);

#define DUMP_PACKET_SIZE(PPKT) \
	(sizeof(*PPKT) + sizeof((PPKT)->dp_fds) * (PPKT)->dp_fds + (PPKT)->dp_data)

#define DUMP_PFDS(PACKET) \
	((dump_fd_t *)(&(PACKET)->bytes[0]))

#define DUMP_PDATA_RAW(PACKET) \
	((uint8_t *)(&(PACKET)->bytes[sizeof(dump_fd_t) * (PACKET)->dp_fds]))

#define DUMP_PDATA(PACKET, TYPE) \
	((TYPE *)(&(PACKET)->bytes[sizeof(dump_fd_t) * (PACKET)->dp_fds]))

#endif // DUMP_H
