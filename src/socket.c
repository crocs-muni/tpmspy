#include "socket.h"

#include <assert.h>
#include <memory.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "defs.h"
#include "strscpy.h"

private
int _get_sock_un(struct sockaddr_un *sun, const char *path)
{
	assert(sun != NULL);

	memset(sun, 0, sizeof(*sun));

	sun->sun_family = AF_UNIX;
	if (strscpy(sun->sun_path, path, sizeof(sun->sun_path)) < 0)
		return -1;

	return socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
}

bool socket_close(struct socket *of)
{
	assert(of != NULL);

	if (of->fd == -1)
		return true;

	int is_listening;
	socklen_t is_listening_size = sizeof(is_listening);

	if (getsockopt(of->fd, SOL_SOCKET, SO_ACCEPTCONN, &is_listening, &is_listening_size) < 0)
		return false;

	bool rv = true;
	if (close(of->fd) < 0)
		rv = false;

	if (is_listening && of->source.type == SOCKET_SOURCE_PATH
			&& unlink(of->source.path) < 0)
		rv = false;

	return rv;
}

bool sock_server(const char *path, struct socket *of)
{
	assert(path != NULL);
	assert(of != NULL);

	struct sockaddr_un address;
	int sock_fd = _get_sock_un(&address, path);

	if (sock_fd < 0)
		return false;

	if (bind(sock_fd, (struct sockaddr *) &address, sizeof(address)) < 0)
		return false;

	if (listen(sock_fd, 2) < 0)
		return false;

	*of = (struct socket) {
		.type = SOCKET_TYPE_SERVER,
		.fd = sock_fd,
		.source = {
			.type = SOCKET_SOURCE_PATH,
			.path = path,
		},
	};

	return true;
}

bool sock_client(const char *path, struct socket *of)
{
	struct sockaddr_un address;
	int sock_fd = _get_sock_un(&address, path);

	if (sock_fd < 0)
		return false;

	if (connect(sock_fd, (struct sockaddr *) &address, sizeof(address)) < 0)
		return false;

	*of = (struct socket) {
		.type = SOCKET_TYPE_CLIENT,
		.fd = sock_fd,
		.source = {
			.type = SOCKET_SOURCE_PATH,
			.path = path,
		},
	};

	return true;
}


