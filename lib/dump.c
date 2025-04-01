#include "dump.h"

#include <string.h>

#include <endian.h>

#define DUMP_BE(N, V) ((V) = be ## N ## toh((V)))
#define BE32(V) DUMP_BE(32, V)
#define BE64(V) DUMP_BE(64, V)

void dump_packet_marshall_inplace(struct dump_packet *p)
{
	BE32(p->dp_src.fd);
	BE32(p->dp_src.type);
	BE32(p->dp_dst.fd);
	BE32(p->dp_dst.type);

	BE64(p->dp_time.s);
	BE32(p->dp_time.us);

	BE32(p->dp_fds);
	BE32(p->dp_data);
}

void dump_packet_marshall(struct dump_packet *dst, const struct dump_packet *src)
{
	memcpy(dst, src, sizeof(*dst));
	dump_packet_marshall_inplace(dst);
}
