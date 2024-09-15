#include "socket.h"

#include <assert.h>
#include <memory.h>

#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "defs.h"
#include "strscpy.h"

ssize_t socket_type_str3(size_t buffer_size, char buffer[buffer_size],
		struct socket_type_str_opt opt)
{
	buffer[0] = '\0';

	char tmp[32];
	size_t end = 0;

	if (opt.type & SOCKET_LINK_QEMU)
		end = strlcat(buffer, "qemu", buffer_size);
	else if (opt.type & SOCKET_LINK_SWTPM)
		end = strlcat(buffer, "swtpm", buffer_size);
	else
		end = strlcat(buffer, "unknown", buffer_size);

	switch (socket_type(opt.type)) {
	case SOCKET_TYPE_CLIENT:
		end = strlcat(buffer, " client", buffer_size);
		break;
	case SOCKET_TYPE_SERVER:
		end = strlcat(buffer, " server", buffer_size);
		break;
	default:
		snprintf(tmp, sizeof(tmp), "unknown (%02x)", socket_type(opt.type));
		end = strlcat(buffer, "tmp", buffer_size);
	}

	return end;

}

ssize_t socket_type_str(size_t buffer_size, char buffer[buffer_size], enum socket_type type)
{
	return socket_type_str3(buffer_size, buffer, (struct socket_type_str_opt){
		.type = type,
		.flags = STSOF_NONE,
	});
}

ssize_t socket_source_str(size_t buffer_size, char buffer[buffer_size],
		const struct socket_source *src)
{
	if (src->type == SOCKET_SOURCE_PATH)
		return strlcpy(buffer, src->path, buffer_size);

	if (src->type == SOCKET_SOURCE_FD)
		return snprintf(buffer, buffer_size, "fd:%d", src->fd);

	return strlcpy(buffer, "(unknown)", buffer_size);
}

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

bool socket_server(const char *path, struct socket *of)
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

bool socket_client(const char *path, struct socket *of)
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


