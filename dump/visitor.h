#pragma once
#ifndef VISITOR_H
#define VISITOR_H

#include <inttypes.h>

#include <dump.h>

#include "dump-structs.h"
#include "options.h"

struct packet_ctrl {
	struct dump_packet_endpoint src, dst;
	struct dump_time time;

	uint32_t cmd;
	uint32_t fds_sz;
	dump_fd_t *fds;

	uint32_t data_sz;
	uint8_t *data;
};

struct packet_data {
	struct dump_packet_endpoint src, dst;
	struct dump_time time;

	uint32_t cmd;
	uint32_t fds_sz;
	dump_fd_t *fds;

	uint32_t data_sz;
	uint8_t *data;

	const struct tpm_req_header *req_header;
	const struct tpm2_send_command_prefix *tpm2_send_prefix;
};

struct dump_visitor {
	void *(*create)(struct resources *res);
	bool  (*destroy)(void *);

#define X(PREFIX, NAME, TYPE) \
	struct { \
		bool (*req)(void *, const struct packet_ctrl *, const TYPE *); \
		bool (*res)(void *, const struct packet_ctrl *, const TYPE *); \
	} v_ctrl_ ## NAME;
	TPM_CMD(X)
#undef X

	struct {
		bool (*req)(void *, const struct packet_ctrl *);
		bool (*res)(void *, const struct packet_ctrl *);
		bool (*generic)(void *, const struct packet_ctrl *);
	} v_ctrl_unknown;

#define X(PREFIX, NAME, TYPE) \
	struct { \
		bool (*req)(void *, const struct packet_data *, const TYPE *); \
		bool (*res)(void *, const struct packet_data *, const TYPE *); \
	} v_data_ ## NAME;
	TPM_CC(X)
#undef X

	struct {
		bool (*req)(void *, const struct packet_data *);
		bool (*res)(void *, const struct packet_data *);
		bool (*generic)(void *, const struct packet_data *);
	} v_data_unknown;
};

#endif // VISITOR_H
