/*
 * swtpm          sockspy              qemu
 *     □ ◀─────── ▣     □▣ ◀────────── □
 *                ┊     ┊┊
 *                ┊     ┊┊
 *   TPM_CLIENT  ┄┘     ┊└┄┄ QEMU_CLIENT
 *                      └┄┄┄ QEMU_SERVER
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <ev.h>

#include "defs.h"
#include "dump.h"
#include "socket.h"

#define CONFIG_OUTPUT_COLS 20

struct options {
	struct io log;
	struct io dump;
};

struct ev_conntrack {
	bool upstream;
	size_t clients;
};

struct ev_sockdata {
	struct ev_sockdata *_next;
	struct ev_context *context;

	struct socket sock;
	enum socket_kind kind;
	ev_io watcher;

	const char *label;
};


struct ev_context {
	struct ev_sockdata *socks;
	struct ev_conntrack conns;

	char *buffer;
	size_t buffer_size;
	ev_signal w_int;
	const struct options *options;
};

private
void _evc_add_socket(struct ev_context *evc, struct ev_sockdata *sockdata)
{
	assert(evc != NULL);
	assert(sockdata != NULL);

	sockdata->_next = NULL;
	if (evc->socks == NULL) {
		evc->socks = sockdata;
		return;
	}

	struct ev_sockdata *current = evc->socks;
	while (current->_next != NULL)
		current = current->_next;

	current->_next = sockdata;
}

private
struct ev_sockdata * _sock_find(struct ev_context *evc, int fd)
{
	assert(evc != NULL);
	assert(fd >= 0);

	struct ev_sockdata *cursor = evc->socks;
	while (cursor != NULL) {
		if (cursor->sock.fd == fd)
			return cursor;

		cursor = cursor->_next;
	}

	return NULL;
}

const struct ev_sockdata *evc_add(struct ev_context *evc, const struct socket *sock,
		enum socket_kind kind, const char *label)
{
	assert(evc != NULL);
	assert(sock != NULL);

	struct ev_sockdata *entry = malloc(sizeof(*entry));

	if (entry == NULL)
		return NULL;

	entry->context = evc;
	entry->sock = *sock;
	entry->kind = kind;
	entry->label = label;

	_evc_add_socket(evc, entry);
	return entry;
}

struct ev_sockdata *evc_next(struct ev_context *evc, struct ev_sockdata *cursor,
		enum socket_kind kind)
{
	cursor = cursor == NULL ? evc->socks : cursor->_next;

	while (cursor != NULL) {
		if ((cursor->kind & kind) != 0)
			return cursor;

		cursor = cursor->_next;
	}

	return NULL;
}

#define each_socket(VAR, EVC, KIND) \
	struct ev_sockdata *VAR = evc_next((EVC), NULL, (KIND)); VAR != NULL; \
		VAR = evc_next((EVC), VAR, (KIND))

private
bool _sock_alloc(struct ev_context *context)
{
	if (context->buffer != NULL)
		return true;

	long page_size = sysconf(_SC_PAGESIZE);
	context->buffer_size = 4 * page_size;
	return (context->buffer = malloc(context->buffer_size)) != NULL;
}

typedef void (ev_io_cb)(struct ev_loop *loop, ev_io *watcher, int revents);

private
bool evc_io(struct ev_loop *loop, const struct ev_sockdata *data, ev_io_cb *cb)
{
	/* Safe typecast: We know we own the data, we just want to prevent
	 * the rest of the code from modifying attributes by accident. */
	ev_io *watcher = (ev_io *) &data->watcher;

	ev_init(watcher, cb);
	ev_io_set(watcher, data->sock.fd, EV_READ);

	watcher->data = (void *) data;
	ev_io_start(loop, watcher);
	return true;
}

private
bool _write_dump(struct ev_sockdata *data, size_t pckt_len, const void *pckt)
{
	const struct io *dump = &data->context->options->dump;

	if (dump->type != IO_STD)
		return true;

	return write(dump->std, pckt, pckt_len) == (ssize_t) pckt_len;
}

#define log(LOG, FORMAT, ...) \
	if ((LOG)->type == IO_STD) \
		dprintf((LOG)->std, FORMAT "\n" __VA_OPT__(,) __VA_ARGS__)

private
ssize_t _socket_splice(struct ev_sockdata *data, enum socket_kind target)
{
	if (!_sock_alloc(data->context))
		err(EXIT_FAILURE, "Cannot allocate socket buffer");

	const struct io *log = &data->context->options->log;

	log(log, "IO from %2d %s", data->sock.fd, data->label);

	ssize_t rd, total = 0;
	while ((rd = read(data->sock.fd, data->context->buffer, data->context->buffer_size)) > 0) {
		total += rd;

		for (each_socket(sock, data->context, target)) {
			log(log, "   to   %2d %s", sock->sock.fd, sock->label);
			struct dump_packet_tx tx = {
				.dp_kind = DUMP_TRANSFER,
				.dp_tx_src_kind = data->kind,
				.dp_tx_src = data->sock.fd,
				.dp_tx_dst_kind = sock->kind,
				.dp_tx_dst = sock->sock.fd,
			};

			_write_dump(data, sizeof(tx), &tx);

			ssize_t wr = 0, written = 0;
			while (written < rd && (wr = write(sock->sock.fd,
					data->context->buffer + written, rd - written)) > 0) {
				written += wr;
			}

			if (wr == -1)
				log(log, "IO err  %2d %s: %s", sock->sock.fd, sock->label, strerror(errno));
		}

		struct dump_packet_data dt = {
			.dp_kind = DUMP_DATA,
			.dp_data_len = rd,
		};

		_write_dump(data, sizeof(dt), &dt);
		_write_dump(data, rd, data->context->buffer);
	}

	log(log, "   size %8zd", total);

	if (rd == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return total;

		log(log, "FD err  %2d %s: %s", data->sock.fd, data->label, strerror(errno));
		return -1;
	}

	if (rd == 0) {
		log(log, "FD down %2d %s", data->sock.fd, data->label);
		return -1;
	}

	return total;
}

private
void _evc_check_exit(struct ev_loop *loop, struct ev_context *context)
{
	if (!context->conns.upstream) {
		fprintf(stderr, "TPM socket disconnected, quitting\n");
		ev_break(loop, EVBREAK_ALL);
	}
}

private
void qemu_splice(struct ev_loop *loop, ev_io *watcher, unused int revents)
{
	if (_socket_splice(watcher->data, SOCKIND_TPM_CLIENT) == -1) {
		ev_io_stop(loop, watcher);
		struct ev_sockdata *data = watcher->data;
		data->context->conns.clients--;

		_evc_check_exit(loop, data->context);
	}
}

private
void tpm_splice(struct ev_loop *loop, ev_io *watcher, unused int revents)
{
	if (_socket_splice(watcher->data, SOCKIND_QEMU_CLIENT) == -1) {
		ev_io_stop(loop, watcher);

		struct ev_sockdata *data = watcher->data;
		data->context->conns.upstream = false;

		_evc_check_exit(loop, data->context);
	}
}

private
void qemu_srv_accept(struct ev_loop *loop, ev_io *watcher, int revents)
{
	if ((revents & (POLL_ERR | POLL_HUP)) != 0)
		errx(EXIT_FAILURE, "QEMU: ERR or HUP");

	struct socket *sock_client = malloc(sizeof(*sock_client));
	if (sock_client == NULL)
		err(EXIT_FAILURE, "QEMU: accept()");

	struct ev_sockdata *data = watcher->data;

	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);

	if ((sock_client->fd = accept(watcher->fd, (struct sockaddr *) &addr, &addrlen)) == -1)
		err(EXIT_FAILURE, "QEMU: accept()");

	int flags = fcntl(sock_client->fd, F_GETFL);
	/* This can only fail if we pass ‹fcntl()› an invalid fd or argument. */
	assert(flags != -1);

	if (fcntl(sock_client->fd, F_SETFL, flags | O_NONBLOCK) == -1)
		err(EXIT_FAILURE, "QEMU: fcntl()");

	sock_client->source = (struct socket_source) {
		.type = SOCKET_SOURCE_FD,
		.fd = watcher->fd,
	};

	const struct io *log = &data->context->options->log;
	log(log, "FD conn %2d %s", data->sock.fd, data->label);
	log(log, "   acpt %2d", sock_client->fd);

	const struct ev_sockdata *sd_client;
	if ((sd_client = evc_add(data->context, sock_client, SOCKIND_QEMU_CLIENT, "qemu(client)")) == NULL)
		err(EXIT_FAILURE, "QEMU: evc_add()");

	evc_io(loop, sd_client, &qemu_splice);
	data->context->conns.clients++;
}

typedef void (signal_cb_t)(struct ev_loop *, ev_signal *, int);

private
void stop_cb(struct ev_loop *loop, unused struct ev_signal *watcher, unused int revents)
{
	warnx("Deadly signal received");
	ev_break(loop, EVBREAK_ALL);
}

private inline
bool evc_signal(struct ev_loop *loop, struct ev_signal *watcher, int signum, signal_cb_t *cb)
{
	ev_signal_init(watcher, cb, signum);
	ev_signal_start(loop, watcher);
	return true;
}

private
bool run_relay(const struct options *options, const struct socket *sock_qemu_srv,
		const struct socket *sock_tpm)
{
	struct ev_context evc = { .options = options };
	const struct ev_sockdata *sd_qemu_srv, *sd_tpm;

	if ((sd_qemu_srv = evc_add(&evc, sock_qemu_srv, SOCKIND_QEMU_SERVER, "qemu(srv)")) == NULL)
		err(EXIT_FAILURE, "Cannot add QEMU server socket");

	if ((sd_tpm = evc_add(&evc, sock_tpm, SOCKIND_TPM_CLIENT, "swtpm")) == NULL)
		err(EXIT_FAILURE, "Cannot add swtpm socket socket");

	struct ev_loop *loop = ev_loop_new(EVFLAG_AUTO | EVFLAG_SIGNALFD);

	if (!evc_io(loop, sd_qemu_srv, &qemu_srv_accept) || !evc_io(loop, sd_tpm, &tpm_splice))
		err(EXIT_FAILURE, "Cannot add EV watchers to the loop");

	evc.conns.upstream = true;

	if (!evc_signal(loop, &evc.w_int, SIGINT, &stop_cb))
		warn("Failed to setup SIGINT handler");
	if (!evc_signal(loop, &evc.w_int, SIGTERM, &stop_cb))
		warn("Failed to setup SIGTERM handler");

	ev_run(loop, 0);

	return true;
}

const char SHORT_OPTS[] = "hL:D:";
const struct option LONG_OPTS[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "log-file", required_argument, NULL, 'L' },
	{ "dump-file", required_argument, NULL, 'D' },

	{ 0 },
};

private
void usage(FILE *stream)
{
	extern const char *__progname;
	fprintf(stream, "usage: %s SOCK_SWTPM SOCK_QEMU\n", __progname);
}

private
void options_process(struct options *options, int *argc, char **argv)
{
	int option;
	while ((option = getopt_long(*argc, argv, SHORT_OPTS, LONG_OPTS, NULL)) != -1) {
		switch (option) {
		case 'h':
			usage(stdout);
			exit(EXIT_SUCCESS);

		case 'D':
			if ((options->dump.std = open(optarg, O_WRONLY | O_CREAT | O_TRUNC, 0600)) == -1)
				err(EXIT_FAILURE, "%s", optarg);
			options->dump.type = IO_STD;
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
		.dump = UNINITIALISED_IO,
		.log = UNINITIALISED_IO,
	};

	options_process(&options, &argc, argv);

	if (argc != 3) {
		usage(stderr);
		return EXIT_FAILURE;
	}

	int status = EXIT_FAILURE;

	if (options.log.type == IO_CLOSED) {
		options.log.type = IO_STD;
		options.log.std = STDOUT_FILENO;
	}

	struct socket sock_swtpm, sock_qemu_srv;
	if (!sock_client(argv[1], &sock_swtpm)) {
		warn("%s: Cannot start client", argv[1]);
		goto leave;
	}

	if (!sock_server(argv[2], &sock_qemu_srv)) {
		warn("%s: Cannot start server", argv[2]);
		goto close_sock_swtpm;
	}

	if (!run_relay(&options, &sock_qemu_srv, &sock_swtpm)) {
		goto close_sock_qemu;
	}

close_sock_qemu:
	if (!socket_close(&sock_qemu_srv))
		warn("%s", sock_qemu_srv.source.path);

close_sock_swtpm:
	if (!socket_close(&sock_swtpm))
		warn("%s", sock_swtpm.source.path);

leave:
	return status;
}
