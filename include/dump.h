#if !defined(DUMP_H)
#define DUMP_H

#include <stddef.h>

#include "socket.h"

struct dump_packet_endpoint {
	enum socket_type type;
	int fd;
};

struct dump_packet {
	struct dump_packet_endpoint dp_src, dp_dst;

	size_t dp_fds;
	size_t dp_data;
	char bytes[];
};

#define DUMP_PACKET_SIZE(PPKT) \
	(sizeof(*PPKT) + sizeof(int) * (PPKT)->dp_fds + (PPKT)->dp_data)

#define DUMP_SET_LINK(ATTR, LNK) \
	ATTR = (struct dump_packet_endpoint){ .type = (LNK)->sock.type, .fd = (LNK)->sock.fd }

#define DUMP_PFDS(PACKET) \
	((int *)(&(PACKET)->bytes[0]))

#define DUMP_PDATA(PACKET) \
	((int8_t *)(&(PACKET)->bytes[sizeof(int) * (PACKET)->dp_fds]))

#endif // DUMP_H
