#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <err.h>
#include <fcntl.h>
#include <unistd.h>

#include <defs.h>
#include <dump.h>
#include <msg.h>
#include <socket.h>

#include "dump-structs.h"
#include "fdcache.h"
#include "jsonv.h"
#include "options.h"
#include "reader.h"
#include "visitor.h"

/* Packet dumping utilities
 * ------------------------ */

#define HAS_FLAGS(V, FLAGS) \
	(((V) & (FLAGS)) == (FLAGS))

struct dump_ctx {
	const struct options *opt;
	struct resources *res;

	const struct dump_visitor *vmt;
	void *vmt_ctx;
};

private
void _dump_packet_raw(const struct dump_packet *pkt)
{
	char buf[256];
	size_t cursor = 0;

	uint8_t *data = DUMP_PDATA(pkt, uint8_t);

	for (size_t i = 0; i < pkt->dp_data; i++) {
		if (i % 16 == 0) {
			cursor = 0;
			buf[0] = '\0';
		}

		if (i % 8 == 0)
			buf[cursor++] = ' ';

		cursor += snprintf(&buf[cursor], sizeof(buf) - cursor, " %02x", data[i]);
		strcat(buf, "\n");

		if (i % 16 == 15 || i + 1 == pkt->dp_data)
			fputs(buf, stderr);
	}

	fputc('\n', stderr);
}

private
bool _packet_unwrap_ctrl(struct packet_ctrl *target, const struct dump_packet *pkt, size_t)
{
	memset(target, 0, sizeof(*target));

	target->src = pkt->dp_src;
	target->dst = pkt->dp_dst;
	target->time = pkt->dp_time;

	target->cmd = be32toh(*DUMP_PDATA(pkt, uint32_t));

	target->fds_sz = pkt->dp_fds;
	target->fds = DUMP_PFDS(pkt);

	target->data_sz = pkt->dp_data;
	target->data = DUMP_PDATA_RAW(pkt);

	return true;
}

private
bool _packet_unwrap_data(struct packet_data *target, const struct dump_packet *pkt, size_t pktsz)
{
	if (pktsz < sizeof(*target->req_header))
		return false;

	_packet_unwrap_ctrl((struct packet_ctrl *) target, pkt, pktsz);

	target->req_header = DUMP_PDATA(pkt, struct tpm_req_header);
	target->cmd = be32toh(target->req_header->ordinal);

	off_t offset;
	int32_t tag = be16toh(target->req_header->tag);
	if (tag == TPM2_ST_NO_SESSION || tag == TPM2_ST_SESSIONS) {
		offset = 0;
	} else {
		offset = sizeof(*target->tpm2_send_prefix);
		target->tpm2_send_prefix = DUMP_PDATA(pkt, struct tpm2_send_command_prefix);
	}

	target->data_sz = pkt->dp_data - (sizeof(*target->req_header) - offset);
	target->data = DUMP_PDATA_RAW(pkt) + sizeof(*target->req_header) + offset;

	return true;
}

/* Requests */

// https://github.com/stefanberger/swtpm/blob/ad4427ab8c4f27cff45be6ac55af0d6beddd88ad/src/swtpm/mainloop.c#L176

private
bool _dump_packet_ctrl_req(struct dump_ctx *dctx, const struct dump_packet *pkt, size_t pktsz)
{
	struct packet_ctrl packet;
	if (!_packet_unwrap_ctrl(&packet, pkt, pktsz))
		return warn_v(false, "Cannot unwrap control packet");

	if (!fd_cache_store(&dctx->res->fdc, pkt->dp_src.fd, packet.cmd))
		warn("comm_cache_store(%d, %d)", pkt->dp_src.fd, packet.cmd);

	fprintf(stderr, "\nCTRL REQUEST BEGIN\n");
	_dump_packet_raw(pkt);

	switch (packet.cmd) {
#define EXPAND_TPM_COMMAND(P, NAME, TYPE) \
	case P ## NAME: \
		if (dctx->vmt->v_ctrl_ ## NAME.req != nullptr) \
			return dctx->vmt->v_ctrl_ ## NAME.req(dctx->vmt_ctx, &packet, DUMP_PDATA(pkt, const TYPE)); \
		break;
	TPM_CMD(EXPAND_TPM_COMMAND)
#undef EXPAND_TPM_COMMAND

	default:
		if (dctx->vmt->v_ctrl_unknown.req != nullptr)
			return dctx->vmt->v_ctrl_unknown.req(dctx->vmt_ctx, &packet);
	}

	bug("Unhandled ctrl request: %08x", packet.cmd);
	return true;
}

private
bool _dump_packet_data_req(struct dump_ctx *dctx, const struct dump_packet *pkt, size_t pktsz)
{
	struct packet_data packet;
	if (!_packet_unwrap_data(&packet, pkt, pktsz))
		return warn_v(false, "Cannot unwrap data packet");

	if (!fd_cache_store(&dctx->res->fdc, pkt->dp_src.fd, packet.cmd))
		warn("comm_cache_store(%d, %d)", pkt->dp_src.fd, packet.cmd);

	fprintf(stderr, "\nDATA REQUEST BEGIN\n");
	_dump_packet_raw(pkt);

	switch (packet.cmd) {
#define X(P, NAME, TYPE) \
	case P ## NAME: \
		if (dctx->vmt->v_data_ ## NAME.req != nullptr) \
			return dctx->vmt->v_data_ ## NAME.req(dctx->vmt_ctx, &packet, (const TYPE *) packet.data); \
		break;
	TPM_CC(X)
#undef X

	default:
		if (dctx->vmt->v_data_unknown.req != nullptr)
			return dctx->vmt->v_data_unknown.req(dctx->vmt_ctx, &packet);
	}

	bug("Unhandled data request: %08x", packet.cmd);
	fprintf(stderr, "tag=%04x ordinal=%04x size=%08x\n",
		be16toh(packet.req_header->tag), be32toh(packet.req_header->ordinal), be32toh(packet.req_header->size));

	return true;
}


/* Responses */

private
bool _dump_packet_ctrl_res(struct dump_ctx *dctx, const struct dump_packet *pkt, size_t pktsz, uint32_t command)
{
	struct packet_ctrl packet;
	if (!_packet_unwrap_ctrl(&packet, pkt, pktsz))
		return warn_v(false, "Cannot unwrap control packet");

	fprintf(stderr, "\nCTRL RESPONSE BEGIN\n");
	_dump_packet_raw(pkt);

	switch (command) {
#define EXPAND_TPM_COMMAND(P, NAME, TYPE) \
	case P ## NAME: \
		if (dctx->vmt->v_ctrl_ ## NAME.res != nullptr) \
			return dctx->vmt->v_ctrl_ ## NAME.res(dctx->vmt_ctx, &packet, DUMP_PDATA(pkt, const TYPE)); \
		break;
	TPM_CMD(EXPAND_TPM_COMMAND)
#undef EXPAND_TPM_COMMAND

	default:
		if (dctx->vmt->v_ctrl_unknown.res != nullptr)
			return dctx->vmt->v_ctrl_unknown.res(dctx->vmt_ctx, &packet);

	}

	bug("Unhandled ctrl response: %08x", command);
	return true;
}

private
bool _dump_packet_data_res(struct dump_ctx *dctx, const struct dump_packet *pkt, size_t pktsz, uint32_t command)
{
	struct packet_data packet;
	if (!_packet_unwrap_data(&packet, pkt, pktsz))
		return warn_v(false, "Cannot unwrap data packet");

	fprintf(stderr, "DATA RESPONSE BEGIN\n");
	_dump_packet_raw(pkt);

	switch (command) {
#define EXPAND_TPM_DATA(P, NAME, TYPE) \
	case P ## NAME: \
		if (dctx->vmt->v_data_ ## NAME.res != nullptr) \
			return dctx->vmt->v_data_ ## NAME.res(dctx->vmt_ctx, &packet, DUMP_PDATA(pkt, const TYPE)); \
		break;
	TPM_CC(EXPAND_TPM_DATA)
#undef EXPAND_TPM_COMMAND

	default:
		if (dctx->vmt->v_data_unknown.res != nullptr)
			return dctx->vmt->v_data_unknown.res(dctx->vmt_ctx, &packet);

	}

	bug("Unhandled data response: %08x", command);
	fprintf(stderr, "tag=%04x ordinal=%04x size=%08x\n",
		be16toh(packet.req_header->tag), be32toh(packet.req_header->ordinal), be32toh(packet.req_header->size));

	return true;
}

/* Generic */

private
bool _dump_packet_ctrl_unknown(struct dump_ctx *dctx, const struct dump_packet *pkt, size_t pktsz)
{
	if (dctx->vmt->v_ctrl_unknown.generic == nullptr)
		return true;

	struct packet_ctrl packet;
	if (!_packet_unwrap_ctrl(&packet, pkt, pktsz))
		return warn_v(false, "Failed to unwrap unknown control packet");

	return dctx->vmt->v_ctrl_unknown.generic(dctx->vmt_ctx, &packet);
}

private
bool _dump_packet(struct dump_ctx *dctx, const struct dump_packet *pkt, size_t pktsz)
{
	if (pkt->dp_src.type & SOCKET_TYPE_CLIENT) {
		if (pkt->dp_src.type & SOCKET_LINK_QEMU)
			return _dump_packet_ctrl_req(dctx, pkt, pktsz);
		if (pkt->dp_src.type & SOCKET_LINK_SWTPM)
			return _dump_packet_ctrl_res(dctx, pkt, pktsz, fd_cache_load(dctx->res->fdc, pkt->dp_dst.fd));

		return _dump_packet_ctrl_unknown(dctx, pkt, pktsz);
	}

	if (HAS_FLAGS(pkt->dp_src.type, SOCKET_TYPE_LINK)) {
		if (pkt->dp_src.type & SOCKET_LINK_QEMU)
			return _dump_packet_data_req(dctx, pkt, pktsz);
		if (pkt->dp_src.type & SOCKET_LINK_SWTPM)
			return _dump_packet_data_res(dctx, pkt, pktsz, fd_cache_load(dctx->res->fdc, pkt->dp_dst.fd));
	}

	return true;
}


/* Packet loading utilities
 * ------------------------ */

private
const struct dump_visitor *_dump_visitor_load(const struct options *options)
{
	if (options->json)
		return &JSON_VISITOR;

	fprintf(stderr, "Custom visitors are not supported yet, use '--json'\n");
	return nullptr;
}

private
bool _dump_file_read_header(int fd, struct dump_header *dfh)
{
	ssize_t rd;
	if ((rd = read(fd, dfh, sizeof(*dfh))) == 0) {
		warnx("read(): File too short\n");
		return false;
	}

	if (rd == -1) {
		warn("read()");
		return false;
	}

	char magic[] = DUMP_MAGIC;
	if (memcmp(dfh->magic, magic, sizeof(magic)) != 0) {
		fprintf(stderr, "Magic header mismatch\n");
		return false;
	}

	return true;
}

private
bool _dump_file(const struct options *options, const struct dump_visitor *visitor,
		struct resources *res, int fd)
{
	struct dump_header dfh;
	if (!_dump_file_read_header(fd, &dfh)) {
		fprintf(stderr, "%s: File is corrupt\n", options->path);
		return false;
	}

	const struct dump_reader *reader = dump_file_get_reader(options, &dfh);
	if (reader == nullptr) {
		fprintf(stderr, "%s: Unsupported version %d.%d\n", options->path,
				dfh.version[0], dfh.version[1]);
		return false;
	}

	void *reader_ctx = nullptr;
	if (reader->create != nullptr && (reader_ctx = reader->create(res)) == nullptr)
		return false;

	struct dump_ctx dctx = {
		.opt = options,
		.res = res,
		.vmt = visitor,
	};

	if (visitor->create != nullptr && (dctx.vmt_ctx = visitor->create(res)) == nullptr)
		return false;

	struct dump_packet *pkt = nullptr;
	size_t pktsz = 0U;

	enum dump_reader_status status;
	while ((status = reader->read(reader_ctx, fd, &pkt, &pktsz)) == DR_STATUS_OK) {
		if (!_dump_packet(&dctx, pkt, pktsz)) {
			fprintf(stderr, "_dump_packet() failed\n");
			goto free_pkt;
		}
	}

	if (status == DR_STATUS_ERRNO)
		warn("%s: Failed to fetch next packet", options->path);

free_pkt:
	free(pkt);

	if (visitor->destroy != nullptr && !visitor->destroy(dctx.vmt_ctx))
		warn("Failed to destroy visitor context");

	if (reader->destroy != nullptr && !reader->destroy(reader_ctx))
		warn("Failed to destroy reader context");

	return true;
}

private
bool _resources_init(struct resources *res, const struct options *)
{
	if ((res->fdc = fd_cache_new()) == nullptr) {
		warn("comm_cache_new()");
		return false;
	}

	return true;
}

private
void _resources_destroy(struct resources *res)
{
	free(res->fdc);
}

int main(int argc, char *argv[])
{
	struct options options = {};
	options_process(&options, argc, argv);

	int fd = open(options.path, O_RDONLY);
	if (fd == -1)
		err(EXIT_FAILURE, "%s", options.path);

	struct resources res = {};
	if (!_resources_init(&res, &options))
		err(EXIT_FAILURE, "Cannot initialise resources");

	const struct dump_visitor *visitor = _dump_visitor_load(&options);

	if (visitor == nullptr)
		exit(EXIT_FAILURE);

	_dump_file(&options, visitor, &res, fd);

	_resources_destroy(&res);
	close(fd);
	return EXIT_SUCCESS;
}
