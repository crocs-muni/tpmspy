#if !defined(SOCKET_H)
#define SOCKET_H

#include <stdbool.h>
#include "io.h"

enum socket_type {
	SOCKET_TYPE_SERVER,
	SOCKET_TYPE_CLIENT,
};

struct socket {
	int fd;
	enum socket_type type;

	struct socket_source {
		enum {
			SOCKET_SOURCE_PATH,
			SOCKET_SOURCE_FD,
		} type;

		union {
			const char *path;
			int fd;
		};
	} source;
};


bool sock_server(const char *path, struct socket *of);
bool sock_client(const char *path, struct socket *of);

bool socket_close(struct socket *of);

#endif // SOCKET_H
