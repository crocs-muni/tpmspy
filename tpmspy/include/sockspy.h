#pragma once
#ifndef SOCKSPY_H
#define SOCKSPY_H

#include <ev.h>

#include "io.h"
#include "sinks.h"
#include "socket.h"
#include "strings.h"

struct options {
	struct io log;

	const char *swtpm_path;
	const char *qemu_path;

	struct strings *sinks;
};

struct socket_watcher {
	struct socket_watcher *_next;
	struct context *ctx;

	struct socket sock;
	struct sinks_context *sink_ctx;
	ev_io watcher;
};

struct context {
	struct socket_watcher *socks;
	struct conntrack *conns;
	struct ev_loop *loop;
	const struct sinks *sinks;

	char *buffer;
	size_t buffer_size;
	ev_signal w_int;

	size_t accept_counter;
	const struct options *options;
};

#endif // SOCKSPY_H
