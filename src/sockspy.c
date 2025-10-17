/*
 * swtpm          sockspy              qemu
 *     □ ◀─────── ▣     □▣ ◀────────── □
 *                ┊     ┊┊
 *                ┊     ┊┊
 *   TPM_CLIENT  ┄┘     ┊└┄┄ QEMU_CLIENT
 *                      └┄┄┄ QEMU_SERVER
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <unistd.h>

#include <ev.h>

#include "asserts.h"
#include "capture.h"
#include "conntrack.h"
#include "defs.h"
#include "msg.h"
#include "socket.h"
#include "sockspy.h"

#define CONFIG_OUTPUT_COLS 20

/* Forward declarations */
typedef void (ev_io_cb)(struct ev_loop *loop, ev_io *watcher, int revents);

private
void handle_socket(struct ev_loop *loop, ev_io *watcher, int /*revents*/);

private
bool socket_watcher_io_start(const struct socket_watcher *sw, ev_io_cb *cb);
private
void socket_watcher_io_stop(struct socket_watcher *sw);

private inline
void _fd_ensure_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL);
	/* This can only fail if we pass ‹fcntl()› an invalid fd or argument. */
	assert(flags != -1);

	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
		BUG("fcntl(%d, F_SETFL, +O_NONBLOCK)", fd);
}

private
void _context_link_sw(struct context *ctx, struct socket_watcher *sw)
{
	assert(ctx != nullptr);
	assert(sw != nullptr);

	sw->_next = ctx->socks;
	ctx->socks = sw;
}

struct socket_watcher *context_find_socket(struct context *ctx, int fd)
{
	assert(ctx != nullptr);
	assert(fd >= 0);

	struct socket_watcher *cursor = ctx->socks;
	while (cursor != nullptr) {
		if (cursor->sock.fd == fd)
			return cursor;

		cursor = cursor->_next;
	}

	return nullptr;
}

struct socket_watcher *context_add_socket(struct context *ctx,
		const struct socket *sock)
{
	assert(ctx != nullptr);
	assert(sock != nullptr);

	struct socket_watcher *sw = malloc(sizeof(*sw));

	if (sw == nullptr)
		return nullptr;

	memset(sw, 0, sizeof(*sw));

	sw->ctx = ctx;
	sw->sock = *sock;

	_context_link_sw(ctx, sw);
	return sw;
}

void context_remove_socket(struct context *ctx, const struct socket_watcher *sw)
{
	assert(ctx != nullptr);
	assert(sw != nullptr);

	if (ctx->socks == sw) {
		ctx->socks = sw->_next;
	} else {
		bool found = false;

		for (struct socket_watcher *cursor = ctx->socks; cursor != nullptr && !found;
				cursor = cursor->_next) {
			if (cursor->_next == sw) {
				found = true;
				cursor->_next = sw->_next;
			}
		}

		if (!found) {
			bug("Socket not found");
			return;
		}
	}

	fprintf(stderr, "socket type %d, value %d\n", sw->sock.type, sw->sock.fd);
	if (sw->capture != nullptr)
		tpm_capture_close(sw->capture);

	free((void *) sw);
}

void context_close_socket(struct context *ctx, struct socket_watcher *sw)
{
	socket_close(&sw->sock);
	socket_watcher_io_stop(sw);
	context_remove_socket(ctx, sw);
}

void context_clear_sockets(struct context *ctx)
{
	while (ctx->socks != nullptr) {
		context_remove_socket(ctx, ctx->socks);
	}
}

void context_cleanup(struct context *ctx)
{
	context_clear_sockets(ctx);

	free(ctx->buffer);
	ctx->buffer = nullptr;
}

private
bool _context_make_buffer(struct context *ctx)
{
	if (ctx->buffer != nullptr)
		return true;

	long page_size = sysconf(_SC_PAGESIZE);
	ctx->buffer_size = 4 * page_size;
	return (ctx->buffer = malloc(ctx->buffer_size)) != nullptr;
}

private
bool socket_watcher_io_start(const struct socket_watcher *sw, ev_io_cb *cb)
{
	/* Safe typecast: We know we own the data, we just want to prevent
	 * the rest of the code from modifying attributes by accident. */
	ev_io *watcher = (ev_io *) &sw->watcher;

	ev_init(watcher, cb);
	ev_io_set(watcher, sw->sock.fd, EV_READ);

	watcher->data = (void *) sw;
	ev_io_start(sw->ctx->loop, watcher);
	return true;
}

private
void socket_watcher_io_stop(struct socket_watcher *sw)
{
	ev_io_stop(sw->ctx->loop, &sw->watcher);
}

#define log(LOG, FORMAT, ...) \
	if ((LOG)->type == IO_STD) \
		dprintf((LOG)->std, FORMAT "\n" __VA_OPT__(,) __VA_ARGS__)

private
bool _socket_spawn_conn(struct context *ctx, struct socket *sock_src_new, struct socket_watcher **src_new,
		struct socket *sock_dst_new, struct socket_watcher **dst_new)
{
	if ((*src_new = context_add_socket(ctx, sock_src_new)) == nullptr)
		goto err_src_new;
	if ((*dst_new = context_add_socket(ctx, sock_dst_new)) == nullptr)
		goto err_dst_new;
	if (!conntrack_add(&ctx->conns, conntrack_entry(sock_src_new->fd, sock_dst_new->fd)))
		goto err_conntrack;

	socket_watcher_io_start(*src_new, handle_socket);
	socket_watcher_io_start(*dst_new, handle_socket);
	return true;

err_conntrack:
	context_close_socket(ctx, *dst_new);

err_dst_new:
	context_close_socket(ctx, *src_new);

err_src_new:
	return false;
}

private
int _socket_spawn_link(struct context *ctx, int src_new_fd,
		struct socket_watcher *parent[2], struct socket_watcher *link[2])
{
	_fd_ensure_nonblock(src_new_fd);

	int src_domain, src_type, src_protocol;
	socklen_t optlen = sizeof(int);

	if (getsockopt(src_new_fd, SOL_SOCKET, SO_DOMAIN, &src_domain, &optlen) < 0)
		return warn_v(-1, "getsockopt(SO_DOMAIN)");

	if (getsockopt(src_new_fd, SOL_SOCKET, SO_TYPE, &src_type, &optlen) < 0)
		return warn_v(-1, "getsockopt(SO_TYPE)");

	if (getsockopt(src_new_fd, SOL_SOCKET, SO_PROTOCOL, &src_protocol, &optlen) < 0)
		return warn_v(-1, "getsockopt(SO_PROTOCOL)");

	if (src_domain != AF_UNIX)
		return warnx_v(-1, "Sockets other than AF_UNIX(%d) not supported, (%d) passed", AF_UNIX, src_domain);

	int dst_new_fd[2];
	if (socketpair(src_domain, src_type | SOCK_NONBLOCK, src_protocol, dst_new_fd) < 0)
		return warn_v(-1, "socketpair(%d, %d, %d)", src_domain, src_type, src_protocol);

	/* We will conntrack src_new_fd <-> dst_new_fd[0] and send dst_new_fd[1]
	 * to dst. */
	struct socket sock_src_new = {
		.fd = src_new_fd,
		.type = SOCKET_TYPE_LINK,
		.source = {
			.type = SOCKET_SOURCE_FD,
			.fd = parent[0]->sock.fd,
		},
	};

	struct socket sock_dst_new = {
		.fd = dst_new_fd[0],
		.type = SOCKET_TYPE_LINK,
		.source = {
			.type = SOCKET_SOURCE_FD,
			.fd = parent[1]->sock.fd,
		},
	};

	struct socket_watcher *src_new, *dst_new;
	if (!_socket_spawn_conn(ctx, &sock_src_new, &src_new, &sock_dst_new, &dst_new))
		goto err_close_other;

	/* OK, pass the new dst_new_fd[1] out. */
	if (link != nullptr) {
		link[0] = src_new;
		link[1] = dst_new;
	}

	return dst_new_fd[1];

err_close_other:
	close(dst_new_fd[1]);
	return -1;
}

private
ssize_t _socket_recv(struct context *ctx, struct socket_watcher *src, struct socket_watcher *dst)
{
	const struct io *log = &ctx->options->log;

	struct timeval stamp;
	char message[4096], control[256];

	struct iovec iov[] = {
		{ .iov_base = message, .iov_len = sizeof(message) },
	};

	struct msghdr msg = {
		.msg_iov = iov,
		.msg_iovlen = array_size(iov),
		.msg_control = control,
		.msg_controllen = sizeof(control),
	};

	ssize_t total = 0;

	ssize_t recv;
	while ((recv = recvmsg(src->sock.fd, &msg, 0)) > 0) {
		if (gettimeofday(&stamp, NULL) == -1) {
			warn("gettimeofday()");
			memset(&stamp, 0, sizeof(stamp));
		}

		ssize_t logsize = message[recv - 1] == '\n' ? recv - 1 : recv;
		log(log, "   text %2d: %.*s", src->sock.fd, (int) logsize, message);

		total += recv;

		int new_fd[2] = { -1, -1 };
		struct socket_watcher *new_link[2] = { nullptr, nullptr };
		for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
				cmsg = CMSG_NXTHDR(&msg, cmsg)) {
			if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
				continue;

			int fds_size = cmsg->cmsg_len / sizeof(int);
			int *fds = (int *) CMSG_DATA(cmsg);

			Assert(new_fd[0] == -1, "SCM_RIGHTS: Duplicate headers are not supported");

			for (int i = 0; i < fds_size; ++i) {
				Assert(i != 0 || fds[i] > 0, "SCM_RIGHTS: The fd is not the first");
				Assert(i == 0 || fds[i] <= 0, "SCM_RIGHTS: Multiple fds are not supported");
			}

			new_fd[0] = fds[0];
			log(log, "   lnk1 %2d [via %2d]", new_fd[0], src->sock.fd);

			struct socket_watcher *parent_link[] = { src, dst };
			if ((new_fd[1] = _socket_spawn_link(ctx, new_fd[0], parent_link, new_link)) < 0)
				close(new_fd[0]);

			log(log, "   lnk2 %2d [via %2d]", new_fd[1], dst->sock.fd);

			// TODO: Make new socket tagging more generic.
			new_link[0]->sock.type |= SOCKET_LINK_QEMU;
			new_link[1]->sock.type |= SOCKET_LINK_SWTPM;

			/* The message will be reused, so let's just modify
			 * the descriptor and send it away. */
			*(int *) CMSG_DATA(cmsg) = new_fd[1];
		}

		msg.msg_iov[0].iov_len = recv;
		if (sendmsg(dst->sock.fd, &msg, MSG_NOSIGNAL) == -1)
			warn("sendmsg()");

		if (new_fd[1] != -1) {
			close(new_fd[1]);
			// new_fd[1] = new_link[1]->sock.fd;
		}

		struct socket_watcher *link[] = { src, dst };
		tpm_capture_write(link, &stamp, new_link, &msg.msg_iov[0]);
	}

	log(log, "   size %8zd", total);

	if (recv == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return total;

		log(log, "FD err  %2d: %s", src->sock.fd, strerror(errno));
		return -1;
	}

	if (recv == 0) {
		log(log, "FD down %2d", src->sock.fd);
		return -1;
	}

	return total;
}

private
void _socket_splice_disconnect(struct context *ctx, struct conntrack_entry *link,
		struct socket_watcher *src, struct socket_watcher *dst)
{
	const struct io *log = &ctx->options->log;

	if (link != nullptr) {
		log(log, "   xlnk %2d <-> %2d", link->a, link->b);
		conntrack_remove(&ctx->conns, link);
	}

	struct socket_watcher *watchers[] = { src, dst };
	for (size_t i = 0; i < array_size(watchers); i++) {
		if (watchers[i] == nullptr)
			continue;

		log(log, "IO clse %2d", watchers[i]->sock.fd);
		context_close_socket(ctx, watchers[i]);
	}
}

private
ssize_t _socket_splice(struct socket_watcher *src)
{
	if (!_context_make_buffer(src->ctx))
		err(EXIT_FAILURE, "Cannot allocate socket buffer");

	struct context *ctx = src->ctx;
	const struct io *log = &ctx->options->log;

	socket_type_str(ctx->buffer_size, ctx->buffer, src->sock.type);
	log(log, "IO from %2d [type %02x %s]", src->sock.fd, src->sock.type, ctx->buffer);

	struct conntrack_entry *link = conntrack_find_mut(&ctx->conns, nullptr, src->sock.fd);

	if (link == nullptr) {
		bug("No connection found for %d", src->sock.fd);
		context_close_socket(ctx, src);
		return -1;
	}

	int dst_fd = conntrack_other(*link, src->sock.fd);
	struct socket_watcher *dst = nullptr;

	if ((dst = context_find_socket(ctx, dst_fd)) == nullptr) {
		bug("Socket watcher for %d connected to %d not found", dst_fd, src->sock.fd);
		context_close_socket(ctx, src);
		return -1;
	}

	socket_type_str(ctx->buffer_size, ctx->buffer, dst->sock.type);
	log(log, "   to   %2d [type %02x %s]", dst->sock.fd, dst->sock.type, ctx->buffer);
	ssize_t total = _socket_recv(ctx, src, dst);

	if (total < 0)
		_socket_splice_disconnect(ctx, link, src, dst);

	return total;
}

private
void _handle_socket_check_exit(struct ev_loop *loop, struct context *ctx)
{
	if (ctx->socks == nullptr || (ctx->socks->_next == nullptr && ctx->socks->sock.type & SOCKET_TYPE_SERVER)) {
		fprintf(stderr, "No sockets left, quitting\n");
		ev_break(loop, EVBREAK_ALL);
	}
}

private
void handle_socket(struct ev_loop *loop, ev_io *watcher, int /*revents*/)
{
	struct socket_watcher *data = watcher->data;
	struct context *ctx = data->ctx;

	if (_socket_splice(watcher->data) == -1)
		_handle_socket_check_exit(loop, ctx);
}

private inline
bool _qemu_downlink_accept(struct socket *sock, const struct socket_watcher *sw)
{
	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);

	if ((sock->fd = accept(sw->watcher.fd, (struct sockaddr *) &addr, &addrlen)) == -1)
		return false;

	_fd_ensure_nonblock(sock->fd);

	sock->type = SOCKET_TYPE_CLIENT;
	sock->source = (struct socket_source) {
		.type = SOCKET_SOURCE_FD,
		.fd = sw->watcher.fd,
	};

	return true;
}

private
struct socket_watcher *_qemu_setup_downlink(const struct socket_watcher *sw_qemu_server)
{
	struct socket sock_qemu_client;
	if (!_qemu_downlink_accept(&sock_qemu_client, sw_qemu_server))
		return warn_v(nullptr, "_qemu_setup_downlink(): accept()");

	sock_qemu_client.type |= SOCKET_LINK_QEMU;

	struct socket_watcher *sw_qemu_client = context_add_socket(sw_qemu_server->ctx,
			&sock_qemu_client);

	if (sw_qemu_client == nullptr)
		warn_jmp(close_client, "_qemu_setup_downlink(): context_add_socket()");

	return sw_qemu_client;

close_client:
	close(sock_qemu_client.fd);
	return nullptr;
}

private
struct socket_watcher *_qemu_setup_uplink(struct context *ctx)
{
	struct socket sock_tpm;
	if (!socket_client(ctx->options->swtpm_path, &sock_tpm))
		return warn_v(nullptr, "_qemu_setup_uplink(): Cannot connect to swTPM");

	sock_tpm.type |= SOCKET_LINK_SWTPM;

	struct socket_watcher *sw_tpm = context_add_socket(ctx, &sock_tpm);

	if (sw_tpm == nullptr)
		warn_jmp(close_socket, "_qemu_setup_uplink(): context_add_socket()");

	return sw_tpm;

close_socket:
	close(sock_tpm.fd);
	return nullptr;
}

private
void handle_qemu_connect(struct ev_loop * /*loop*/, ev_io *watcher, int revents)
{
	if ((revents & (POLL_ERR | POLL_HUP)) != 0)
		errx(EXIT_FAILURE, "QEMU: ERR or HUP");

	const struct socket_watcher *sw_qemu_server = watcher->data;
	const struct io *log = &sw_qemu_server->ctx->options->log;

	struct socket_watcher *sw_qemu_client = _qemu_setup_downlink(sw_qemu_server);

	if (sw_qemu_client == nullptr) {
		warn("handle_qemu_connect(): Failed to accept a new client");
		return;
	}

	if ((sw_qemu_client->capture = tpm_capture_open(sw_qemu_server->ctx, sw_qemu_server->ctx->accept_counter++)) == nullptr) {
		warn("handle_qemu_connect(): Failed to open capture file");
		goto fail_qemu_socket;
	}

	char buffer[256];
	socket_type_str(sizeof(buffer), buffer, sw_qemu_server->sock.type);
	log(log, "FD conn %2d [type %02x %s]", sw_qemu_server->sock.fd, sw_qemu_server->sock.type, buffer);

	socket_type_str(sizeof(buffer), buffer, sw_qemu_client->sock.type);
	log(log, "   lnk1 %2d [type %02x %s]", sw_qemu_client->sock.fd, sw_qemu_client->sock.type, buffer);

	struct socket_watcher *sw_tpm = _qemu_setup_uplink(sw_qemu_server->ctx);

	if (sw_tpm == nullptr) {
		warn("handle_qemu_connect(): Failed to connect to swTPM");
		goto fail_qemu_capture;
	}

	if ((sw_tpm->capture = io_dup(sw_qemu_client->capture)) == nullptr) {
		warn("handle_qemu_connect(): Failed to dup capture IO");
		goto fail_swtpm_socket;
	}

	socket_type_str(sizeof(buffer), buffer, sw_tpm->sock.type);
	log(log, "   lnk2 %2d [type %02x %s]", sw_tpm->sock.fd, sw_tpm->sock.type, buffer);

	struct conntrack_entry new_connection = {
		sw_qemu_client->sock.fd,
		sw_tpm->sock.fd,
	};

	if (!conntrack_add(&sw_qemu_server->ctx->conns, new_connection)) {
		warn("handle_qemu_connect(): Conntrack failure");
		goto fail_swtpm_capture;
	}

	socket_watcher_io_start(sw_qemu_client, &handle_socket);
	socket_watcher_io_start(sw_tpm, &handle_socket);
	return;

fail_swtpm_capture:
	tpm_capture_close(sw_tpm->capture);

fail_swtpm_socket:
	close(sw_tpm->sock.fd);
	context_remove_socket(sw_qemu_server->ctx, sw_tpm);

fail_qemu_capture:
	tpm_capture_close(sw_qemu_client->capture);

fail_qemu_socket:
	close(sw_qemu_client->sock.fd);
	context_remove_socket(sw_qemu_server->ctx, sw_qemu_client);
}

typedef void (signal_cb_t)(struct ev_loop *, ev_signal *, int);

private
void stop_cb(struct ev_loop *loop, struct ev_signal * /*watcher*/, int /*revents*/)
{
	warnx("Deadly signal received");
	ev_break(loop, EVBREAK_ALL);
}

private inline
bool setup_signal(struct ev_loop *loop, struct ev_signal *watcher, int signum, signal_cb_t *cb)
{
	ev_signal_init(watcher, cb, signum);
	ev_signal_start(loop, watcher);
	return true;
}

private
bool run_relay(const struct options *options, const struct socket *sock_qemu_srv)
{
	struct ev_loop *loop = ev_loop_new(EVFLAG_AUTO | EVFLAG_SIGNALFD);
	struct context ctx = {
		.loop = loop,
		.options = options,
	};

	if (!conntrack_init(&ctx.conns))
		err(EXIT_FAILURE, "Cannot set up connection tracking");

	const struct socket_watcher *sd_qemu_srv;
	if ((sd_qemu_srv = context_add_socket(&ctx, sock_qemu_srv)) == nullptr)
		err(EXIT_FAILURE, "Cannot add QEMU server socket");

	if (!socket_watcher_io_start(sd_qemu_srv, &handle_qemu_connect))
		err(EXIT_FAILURE, "Cannot add QEMU event watcher to the loop");

	if (!setup_signal(loop, &ctx.w_int, SIGINT, &stop_cb))
		warn("Failed to setup SIGINT handler");
	if (!setup_signal(loop, &ctx.w_int, SIGTERM, &stop_cb))
		warn("Failed to setup SIGTERM handler");

	ev_run(loop, 0);

	conntrack_destroy(&ctx.conns);
	context_cleanup(&ctx);

	return true;
}

const char SHORT_OPTS[] = "hL:D:";
const struct option LONG_OPTS[] = {
	{ "help", no_argument, nullptr, 'h' },
	{ "log-file", required_argument, nullptr, 'L' },
	{ "dump-file", required_argument, nullptr, 'D' },

	{ },
};

private
void usage(FILE *stream)
{
	extern const char *__progname;
	fprintf(stream, "usage: %s [-D|--dump-file=FILE] [-L|--log-file=FILE] SOCK_SWTPM SOCK_QEMU\n", __progname);
}

private
void options_process(struct options *options, int *argc, char **argv)
{
	int option;
	while ((option = getopt_long(*argc, argv, SHORT_OPTS, LONG_OPTS, nullptr)) != -1) {
		switch (option) {
		case 'h':
			usage(stdout);
			exit(EXIT_SUCCESS);

		case 'D':
			char *s;
			if ((s = strchr(optarg, '%')) != nullptr) {
				if (s[1] != 'd')
					errx(EXIT_FAILURE, "-D %s: %% must be followed by d", optarg);
				if (strchr(&s[1], '%') != nullptr)
					errx(EXIT_FAILURE, "-D %s: At most one %% mark is allowed", optarg);
			}

			options->dump_base = optarg;
			break;

		case 'L':
			if ((options->log.std = open(optarg, O_WRONLY | O_CREAT | O_APPEND, 0600)) == -1)
				err(EXIT_FAILURE, "%s", optarg);
			options->log.type = IO_STD;
			break;

		default:
			usage(stderr);
			exit(EXIT_FAILURE);
		}
	}

	if (optind > 0) {
		*argc = *argc - optind + 1;
		for (int i = 1; i <= *argc; ++i) {
			argv[i] = argv[i + optind - 1];
		}
	}
}

int main(int argc, char *argv[])
{
	struct options options = {
		.log = UNINITIALISED_IO,
	};

	options_process(&options, &argc, argv);

	if (argc != 3) {
		usage(stderr);
		return EXIT_FAILURE;
	}

	options.swtpm_path = argv[1];
	options.qemu_path = argv[2];

	int status = EXIT_FAILURE;

	if (options.log.type == IO_CLOSED) {
		options.log.type = IO_STD;
		options.log.std = STDOUT_FILENO;
	}

	struct socket sock_qemu_srv = {};

	if (!socket_server(options.qemu_path, &sock_qemu_srv)) {
		warn("%s: Cannot start server", argv[2]);
		goto leave;
	}

	sock_qemu_srv.type |= SOCKET_LINK_QEMU;

	if (!run_relay(&options, &sock_qemu_srv)) {
		goto close_sock_qemu;
	}

close_sock_qemu:
	if (!socket_close(&sock_qemu_srv))
		warn("%s", sock_qemu_srv.source.path);

leave:
	return status;
}
