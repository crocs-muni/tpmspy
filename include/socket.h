#pragma once
#ifndef SOCKET_H
#define SOCKET_H

#include <stdbool.h>
#include <sys/types.h>

#include "io.h"

enum socket_type {
	SOCKET_TYPE_UNKNOWN = 0,

	SOCKET_TYPE_CLIENT = 1 << 1,
	SOCKET_TYPE_SERVER = 1 << 2,

	SOCKET_TYPE_LINK = 1 << 3,

	SOCKET_TYPE_MASK = SOCKET_TYPE_CLIENT | SOCKET_TYPE_SERVER | SOCKET_TYPE_LINK,
};

#define socket_type(V) \
	((V) & SOCKET_TYPE_MASK)

enum socket_link {
	SOCKET_LINK_QEMU = 1 << 8,
	SOCKET_LINK_SWTPM = 1 << 9,
};

struct socket {
	int fd;
	enum socket_type type;

	struct socket_source {
		enum {
			SOCKET_SOURCE_UNKNOWN,
			SOCKET_SOURCE_PATH,
			SOCKET_SOURCE_FD,
		} type;

		union {
			const char *path;
			int fd;
		};
	} source;
};

struct socket_type_str_opt {
	enum socket_type type;
	enum stso_flag {
		STSOF_NONE = 0,
		STSOF_NUMERIC_PREFIX = 1 << 0,
	} flags;
};

ssize_t socket_type_str3(size_t buffer_size, char buffer[buffer_size], struct socket_type_str_opt opt);
ssize_t socket_type_str(size_t buffer_size, char buffer[buffer_size], enum socket_type t);
ssize_t socket_source_str(size_t buffer_size, char buffer[buffer_size],
		const struct socket_source *src);

bool socket_server(const char *path, struct socket *of);
bool socket_client(const char *path, struct socket *of);

bool socket_close(struct socket *of);

#endif // SOCKET_H
