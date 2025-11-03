#pragma once
#ifndef SINKS_H
#define SINKS_H

#include <stdlib.h>

#include "sockspy.h"
#include "strings.h"

/* Sinks handle messages captured by the TPMSpy.
 *
 * A sink is a module with three methods:
 *   • ‹open()› (optional) opens a new sink context.
 *   • ‹send()› (required) is called when a message is captured.
 *   • ‹close()› (required if ‹open()› is provided) closes the sink.
 *
 * When a connection is created to the main socket, a new set of
 * sinks is instantiated using the ‹open()› calls, if provided.
 * This set of sink contexts is associated with the new connection
 * and will be used for all subsequent channels. This allows a sink
 * implementation to handle multiple clients simultaneously. */

struct sink_message {
	struct timeval *stamp;
	struct socket_watcher *link[2];
	struct socket_watcher *new_link[2];
	struct iovec *payload;
};

// TODO: Reference counting
typedef void *sink_obj;

struct sinks_context; // Opaque.

typedef void *(*sink_open_f)(const struct strings *, const struct context *);
typedef bool (*sink_send_f)(sink_obj obj, const struct sink_message*);
typedef bool (*sink_close_f)(sink_obj obj);

struct sinks {
	size_t size;
	size_t capacity;

	struct {
		const char *name;
		void *dlobj;
		struct strings *args;

		sink_open_f open;
		sink_send_f send;
		sink_close_f close;
	} syms[];
};

[[nodiscard]]
struct sinks *sinks_load(size_t size, char *libs[size]);
void sinks_free(struct sinks **psinks);

[[nodiscard]]
struct sinks_context *sinks_open(const struct sinks *sinks, struct context *ctx);

[[nodiscard]]
struct sinks_context *sinks_dup(struct sinks_context *ctxt);

void sinks_close(const struct sinks *sinks, struct sinks_context *ctxt);

void sinks_dispatch(const struct sinks *sinks, struct sinks_context *ctxt, const struct sink_message *msg);

#endif // SINKS_H
