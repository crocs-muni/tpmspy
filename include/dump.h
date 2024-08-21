#if !defined(DUMP_H)
#define DUMP_H

#include <stddef.h>

enum socket_kind {
	SOCKIND_QEMU_SERVER = 1 << 0,
	SOCKIND_QEMU_CLIENT = 1 << 1,
	SOCKIND_TPM_CLIENT  = 1 << 2,
};

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

	enum socket_kind dp_tx_src_kind;
	int dp_tx_src;
	enum socket_kind dp_tx_dst_kind;
	int dp_tx_dst;
};

struct dump_packet_data {
	enum dump_packet_kind dp_kind;

	size_t dp_data_len;
};

#endif // DUMP_H
