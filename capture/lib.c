#include "sink.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include <err.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>

#include "defs.h"
#include "dump.h"
#include "io.h"
#include "msg.h"
#include "sockspy.h"

struct tpm_capture_context {
	int fd;
};

private
bool _tpm_capture_prologue(const struct tpm_capture_context *tpm_ctx)
{
	return write(tpm_ctx->fd, &DUMP_HEADER, sizeof(DUMP_HEADER)) == sizeof(DUMP_HEADER);
}

private
bool _tpm_capture_epilogue(const struct tpm_capture_context */* tpm_ctx */)
{
	return true;
}

private
bool _tpm_capture_write_packet(int fd, int iovcnt, const struct iovec iov[iovcnt])
{
	ssize_t total = 0;
	for (int i = 0; i < iovcnt; i++)
		total += iov[i].iov_len;

	return writev(fd, iov, iovcnt) == total;
}

private
bool _tpm_capture_write(const struct tpm_capture_context *tpm_ctx, struct timeval *stamp,
		const struct socket_watcher *link[2],
		const struct socket_watcher *new_link[2],
		struct iovec *payload)
{
	struct dump_packet packet = {
		.dp_src = {
			.fd = link[0]->sock.fd,
			.type = link[0]->sock.type,
		},
		.dp_dst = {
			.fd = link[1]->sock.fd,
			.type = link[1]->sock.type,
		},
		.dp_time = {
			.s = stamp->tv_sec,
			.us = stamp->tv_usec,
		},
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
#undef MKIOVEC

	packet.dp_data = payload->iov_len;
	iov[iovix++] = *payload;

	dump_packet_marshall_inplace(&packet);
	return _tpm_capture_write_packet(tpm_ctx->fd, iovix, iov);
}

private
bool _tpm_capture_pattern_is_valid(const char *str)
{
	size_t counter = 0;

	while ((str = strchr(str, '%')) != nullptr) {
		str++;

		if (*str == '%') {
			/* %% */
			str++;
		} else {
			/* %<FLAGS><WIDTH.PRECISION>zu */
			while (*str == ' ' || *str == '-' || *str == '+')
				str++;

			while (isdigit(*str) || *str == '.')
				str++;

			if (*str++ != 'z' && *str++ != 'd')
				return false;

			counter++;
		}
	}

	return counter == 1;
}

private
bool _tpm_capture_open(struct tpm_capture_context *ctx, const char *dir_pattern, size_t n)
{
	assert(dir_pattern != nullptr);

	char *file_name = nullptr;

	if (!_tpm_capture_pattern_is_valid(dir_pattern))
		return warnx_v(false, "capture: %s: Invalid pattern", dir_pattern);

	if (asprintf(&file_name, dir_pattern, n) == -1)
		goto free_file_name;

	if ((ctx->fd = open(file_name, O_WRONLY | O_CREAT | O_EXCL, 0644)) == -1) {
		warn("%s", file_name);
		goto free_file_name;
	}

	if (!_tpm_capture_prologue(ctx))
		goto close_fd;

	free(file_name);
	return true;

close_fd:
	close(ctx->fd);

free_file_name:
	free(file_name);
	return false;
}

private
void _tpm_capture_close(struct tpm_capture_context *ctx)
{
	_tpm_capture_epilogue(ctx);

	close(ctx->fd);
	ctx->fd = -1;
}

[[nodiscard]]
void *sink_open(struct strings *args, const struct context *ctx)
{
	assert(args != nullptr);
	assert(ctx != nullptr);

	if (args->size != 2) {
		warnx("usage: ... --sink capture,OUTPUT_PATTERN ...");
		return nullptr;
	}

	struct tpm_capture_context *tpm_ctx = malloc(sizeof(*ctx));

	if (ctx == nullptr)
		return nullptr;

	if (!_tpm_capture_open(tpm_ctx, args->data[1], ctx->accept_counter))
		goto free_ctx;

	return tpm_ctx;

free_ctx:
	free(tpm_ctx);
	return nullptr;
}

bool sink_send(sink_obj obj, const struct sink_message *msg)
{
	struct tpm_capture_context *tpm_ctx = obj;

	assert(msg != nullptr);
	assert(tpm_ctx != nullptr);
	assert(tpm_ctx->fd >= 0);

	return _tpm_capture_write(tpm_ctx, msg->stamp,
			(const struct socket_watcher **) msg->link,
			(const struct socket_watcher **) msg->new_link,
			msg->payload);
}

bool sink_close(sink_obj obj)
{
	struct tpm_capture_context *tpm_ctx = obj;

	assert(tpm_ctx != nullptr);
	assert(tpm_ctx->fd >= 0);

	_tpm_capture_close(tpm_ctx);
	free(tpm_ctx);

	return true;
}
