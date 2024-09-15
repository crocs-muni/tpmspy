#if !defined(DUMP_H)
#define DUMP_H

#include <stddef.h>

#include "socket.h"

enum dump_packet_kind {
	DUMP_TRANSFER,
	DUMP_DATA,
} dp_kind;

struct dump_packet {
	enum dump_packet_kind dp_kind;
	char data[];
};

struct dump_packet_tx {
	enum dump_packet_kind dp_kind;

	enum socket_type dp_tx_src_type;
	int dp_tx_src;
	enum socket_type dp_tx_dst_type;
	int dp_tx_dst;
};

#define DUMP_TX(SRC, DST) \
	(struct dump_packet_tx){ \
		.dp_kind = DUMP_TRANSFER, \
		.dp_tx_src_type = (SRC)->sock.type, \
		.dp_tx_src = (SRC)->sock.fd, \
		.dp_tx_dst_type = (DST)->sock.type, \
		.dp_tx_dst = (DST)->sock.fd, \
	}

struct dump_packet_data {
	enum dump_packet_kind dp_kind;

	size_t dp_data_len;
};

#define DUMP_DATA(LEN) \
	(struct dump_packet_data){ \
		.dp_kind = DUMP_DATA, \
		.dp_data_len = (LEN), \
	}

#endif // DUMP_H
