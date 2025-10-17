#include "capture.h"

#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>

#include "asserts.h"
#include "defs.h"
#include "dump.h"
#include "io.h"

private
bool tpm_capture_prologue(const struct io *io)
{
	if (io->type != IO_STD)
		return true;

	return write(io->std, &DUMP_HEADER, sizeof(DUMP_HEADER)) == sizeof(DUMP_HEADER);
}

private
bool tpm_capture_epilogue(const struct io * /*dump*/)
{
	return true;
}

private
bool _tpm_capture_write_packet(const struct io *capture, int iovcnt, const struct iovec iov[iovcnt])
{
	ssize_t total = 0;
	for (int i = 0; i < iovcnt; i++)
		total += iov[i].iov_len;

	return writev(capture->std, iov, iovcnt) == total;
}

bool tpm_capture_write(struct socket_watcher *link[2], struct timeval *stamp,
		struct socket_watcher *new_link[2], struct iovec *payload)
{
	Assert(link[0]->capture == link[1]->capture, "src and dst must point to the same capture");
	if (link[0]->capture->type != IO_STD)
		return true;

	struct dump_packet packet = {
		.dp_src = { .fd = link[0]->sock.fd, .type = link[0]->sock.type },
		.dp_dst = { .fd = link[1]->sock.fd, .type = link[1]->sock.type },
		.dp_time = { .s = stamp->tv_sec, .us = stamp->tv_usec },
	};

	struct iovec iov[3];
	size_t iovix = 0;

#define MKIOVEC(SIZE, PTR) \
	(struct iovec){ .iov_len = (SIZE), .iov_base = (PTR) }

	iov[iovix++] = MKIOVEC(sizeof(packet), &packet);

	dump_fd_t fds[2];
	if (new_link[0] != nullptr) {
		packet.dp_fds = 2;
		fds[0] = htobe32(new_link[0]->sock.fd);
		fds[1] = htobe32(new_link[1]->sock.fd);
		iov[iovix++] = MKIOVEC(sizeof(fds), fds);
	}

	packet.dp_data = payload->iov_len;
	iov[iovix++] = *payload;

	dump_packet_marshall_inplace(&packet);
	_tpm_capture_write_packet(link[0]->capture, iovix, iov);
#undef MKIOVEC

	return true;
}


[[nodiscard]]
struct io *tpm_capture_open(struct context *ctx, size_t n)
{
	struct io *io = malloc(sizeof(struct io));
	if (io == nullptr)
		return nullptr;

	io->type = IO_CLOSED;

	if (ctx->options->dump_base == nullptr)
		return io;

	char *file_name = nullptr;

	if (strchr(ctx->options->dump_base, '%') != nullptr) {
		if (asprintf(&file_name, ctx->options->dump_base, n) == -1)
			goto free_io;
	} else {
		if (asprintf(&file_name, "%s.%zu.tpm", ctx->options->dump_base, n) == -1)
			goto free_io;
	}

	if ((io->std = open(file_name, O_WRONLY | O_CREAT | O_EXCL, 0644)) == -1)
		goto free_file_name;

	io->refs = 1;
	io->type = IO_STD;

	if (!tpm_capture_prologue(io))
		goto close_fd;

	free(file_name);
	return io;

close_fd:
	close(io->std);

free_file_name:
	free(file_name);

free_io:
	free(io);
	return nullptr;
}

void tpm_capture_close(struct io *io)
{
	assert(io != nullptr);

	if (io->refs == 1)
		tpm_capture_epilogue(io);

	if (io_close(io))
		free(io);
}
