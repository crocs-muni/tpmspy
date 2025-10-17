#pragma once
#ifndef SOCKSPY_H
#define SOCKSPY_H

#include <ev.h>

#include "io.h"
#include "socket.h"

struct options {
	struct io log;

	const char *dump_base;
	const char *swtpm_path;
	const char *qemu_path;
};

struct socket_watcher {
	struct socket_watcher *_next;
	struct context *ctx;

	struct socket sock;
	struct io *capture;
	ev_io watcher;
};

struct context {
	struct socket_watcher *socks;
	struct conntrack *conns;
	struct ev_loop *loop;

	char *buffer;
	size_t buffer_size;
	ev_signal w_int;

	size_t accept_counter;
	const struct options *options;
};

#endif // SOCKSPY_H
