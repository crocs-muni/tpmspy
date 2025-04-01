/*
 * sws — Simulate passing descriptors via a socket
 *
 * 	$ sws -S|--server SOCKET
 * 	$ sws [-C|--client] SOCKET
 *
 * The server behaves as a client, except it manages the main ‹SOCKET›.
 * Both applications set up a terminal session where they expect commands:
 *
 *  □ ‹/list› prints the list of all active sockets and their IDs.
 *  □ ‹/close N› closes socket ‹N›.
 *  □ ‹/select N› selects a default socket for sending messages.
 *  □ ‹/open [N|$] [MESSAGE…]› creates a new socket and passes it via socket ‹N›,
 *    optionally with ‹MESSAGE…›. Then ‹/select› the new socket automatically.
 *    ‹$› can be used to mean the currenly selected channel.
 *  □ ‹/N MESSAGE…› sends a message to socket ‹N›.
 *  □ ‹/quit› closes all sockets and exits the application.
 *
 * Strings that do not begin with ‹/› are considered messages and will be sent
 * to the currently selected default socket. To send a message beginning with
 * ‹/›, use ‹//MESSAGE›. */

#define _GNU_SOURCE

#define PROBE(EXPR) \
	({ \
		typeof(EXPR) _v = (EXPR); \
		fprintf(stderr, "%% %s = %d\n", #EXPR, _v); \
		_v; \
	 })

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "defs.h"
#include "minmax.h"
#include "msg.h"
#include "sockdb.h"
#include "utils.h"

enum op_mode {
	OP_DEFAULT,
	OP_CLIENT,
	OP_SERVER,
};

struct options {
	enum op_mode mode;
	char *path;
};

enum cli_cmd_type {
	CLI_CMD_NOP,
	CLI_CMD_QUIT,
	CLI_CMD_DIRECT_MSG,
	CLI_CMD_LIST,
	CLI_CMD_SELECT,
	CLI_CMD_CLOSE,
	CLI_CMD_OPEN,
};

struct cli_cmd {
	enum cli_cmd_type cmd_type;
	void *_aux_data;

	union {
		int cmd_fd;

		struct {
			int cmd_msg_via;
			const char *cmd_msg_text;
		};
	};
};

bool parse_int(int *n, const char *str)
{
	errno = 0;
	char *endp;

	long tmp = strtol(str, &endp, 10);
	if (errno != 0 || endp == str || *endp != '\0' || tmp > INT_MAX || tmp < INT_MIN)
		return false;

	*n = tmp;
	return true;
}

private inline
bool _cmd_parse_direct_msg(struct cli_cmd *cmd, char *tokens[1])
{
	return cmd->cmd_msg_via >= 0 && (cmd->cmd_msg_text = tokens[0]) != nullptr;
}

private inline
bool _cmd_parse_fd_arg(struct cli_cmd *cmd, char *tokens[1])
{
	return parse_int(&cmd->cmd_fd, tokens[0]);
}

private inline
bool _cmd_parse_open(struct cli_cmd *cmd, char *tokens[2])
{
	if (streq(tokens[0], "$"))
		cmd->cmd_msg_via = -1;
	else if (!parse_int(&cmd->cmd_msg_via, tokens[0]))
		return false;

	cmd->cmd_msg_text = tokens[1];
	return true;
}

struct cli_cmd_desc {
	enum cli_cmd_type type;
	const char *command;
	bool (*parse)(struct cli_cmd *, char *[]);
	size_t required_tokens;
	bool slurp_rest;
} CLI_COMMANDS[] = {
	{ .type = CLI_CMD_NOP, "" },
	{ .type = CLI_CMD_QUIT, "quit" },
	{ .type = CLI_CMD_DIRECT_MSG, "", &_cmd_parse_direct_msg, 1, true },
	{ .type = CLI_CMD_LIST, "list" },
	{ .type = CLI_CMD_SELECT, "select", &_cmd_parse_fd_arg, 1 },
	{ .type = CLI_CMD_CLOSE, "close", &_cmd_parse_fd_arg, 1 },
	{ .type = CLI_CMD_OPEN, "open", &_cmd_parse_open, 1, true },
	{ },
};

private
const struct option LONG_OPTS[] = {
	{ "help", no_argument, nullptr, 'h' },
	{ "client", no_argument, nullptr, 'C' },
	{ "server", no_argument, nullptr, 'S' },
	{ },
};

private
const char SHORT_OPTS[] = "hCS";

void usage(FILE *stream)
{
	fprintf(stream, "usage: sws [--client|-C|--server|-S] SOCKET\n");
}

private inline
void _options_set_mode(struct options *options, enum op_mode mode)
{
	if (options->mode != OP_DEFAULT && options->mode != mode)
		die("Conflicting mode options");

	options->mode = mode;
}

void options_process(struct options *options, int argc, char *argv[])
{
	memset(options, 0, sizeof(*options));

	int option;
	while ((option = getopt_long(argc, argv, SHORT_OPTS, LONG_OPTS, nullptr)) != -1) {
		switch (option) {
		case 'h':
			usage(stdout);
			exit(EXIT_SUCCESS);

		case 'C':
			_options_set_mode(options, OP_CLIENT);
			break;

		case 'S':
			_options_set_mode(options, OP_SERVER);
			break;

		default:
			exit(EXIT_FAILURE);
		}
	}

	if (options->mode == OP_DEFAULT)
		options->mode = OP_CLIENT;

	if (argc - optind != 1) {
		usage(stderr);
		exit(EXIT_FAILURE);
	}

	options->path = argv[optind];
}

struct client {
	const struct socket *selected;
};

private inline
bool _setup_client_socket(int sock, struct sockaddr_un *sock_addr)
{
	return connect(sock, (struct sockaddr *) sock_addr, sizeof(*sock_addr)) == 0;
}

private inline
bool _setup_server_socket(int sock, struct sockaddr_un *sock_addr)
{
	if (bind(sock, (struct sockaddr *) sock_addr, sizeof(*sock_addr)) != 0)
		return false;

	if (listen(sock, 4) != 0)
		return false;

	return true;
}

struct io *watch_socket(int epfd, struct socket *socket, struct io **ios)
{
	struct io *io = malloc(sizeof(*io));
	if (io == nullptr)
		return nullptr;

	io->type = IO_SOCKET;
	io->socket = socket;

	struct epoll_event ev = {
		.events = EPOLLIN | EPOLLET,
		.data = { .ptr = io, },
	};

	if (epoll_ctl(epfd, EPOLL_CTL_ADD, socket->fd, &ev) != 0) {
		free(io);
		return nullptr;
	}

	return io_chain_append(ios, io);
}

private
bool _fd_add_flags(int fd, int new_flags)
{
	int old_flags;
	if ((old_flags = fcntl(fd, F_GETFL)) == -1)
		return false;

	if (fcntl(fd, F_SETFL, old_flags | new_flags) == -1)
		return false;

	return true;
}

struct io *watch_std(int epfd, int fd, struct io **ios)
{
	if (!_fd_add_flags(fd, O_NONBLOCK))
		return nullptr;

	struct io *io = malloc(sizeof(*io));
	if (io == nullptr)
		return nullptr;

	io->type = IO_STD;
	io->std = fd;

	struct epoll_event ev = {
		.events = EPOLLIN | EPOLLET,
		.data = { .ptr = io, },
	};

	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) != 0) {
		free(io);
		return nullptr;
	}

	return io_chain_append(ios, io);
}

private
sigset_t _get_watched_signals(void)
{
	sigset_t s;
	sigemptyset(&s);
	sigaddset(&s, SIGINT);
	sigaddset(&s, SIGTERM);

	/* These are to be ignored: */
	sigaddset(&s, SIGWINCH);
	sigaddset(&s, SIGPIPE);

	return s;
}

struct io *watch_signals(int epfd, struct io **ios)
{
	sigset_t signals = _get_watched_signals();
	if (sigprocmask(SIG_BLOCK, &signals, nullptr) != 0)
		return nullptr;

	int sigfd = signalfd(-1, &signals, SFD_NONBLOCK);
	if (sigfd == -1)
		return nullptr;

	struct io *io = malloc(sizeof(*io));
	if (io == nullptr) {
		close(sigfd);
		return nullptr;
	}

	io->type = IO_SIGFD;
	io->sigfd = sigfd;

	struct epoll_event ev = {
		.events = EPOLLIN | EPOLLET,
		.data = { .ptr = io, },
	};

	if (epoll_ctl(epfd, EPOLL_CTL_ADD, sigfd, &ev) != 0) {
		close(sigfd);
		free(io);
		return nullptr;
	}

	return io_chain_append(ios, io);
}

struct socket *sockets_init(struct options *options, struct sockdb **sdbp,
		int epfd, struct io **ios)
{
	int sock = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);

	if (sock == -1)
		return nullptr;

	struct sockaddr_un sock_addr = {
		.sun_family = AF_UNIX,
	};

	if (strlcpy(sock_addr.sun_path, options->path, sizeof(sock_addr.sun_path))
			>= sizeof(sock_addr.sun_path)) {
		errno = ENAMETOOLONG;
		return nullptr;
	}

	struct socket socket = {
		.fd = sock,
		.source = {
			.type = SOCKET_SOURCE_PATH,
			.path = options->path,
		},
	};

	if (options->mode == OP_CLIENT) {
		if (!_setup_client_socket(sock, &sock_addr))
			return nullptr;
		socket.type = SOCKET_TYPE_CLIENT;
	} else {
		if (!_setup_server_socket(sock, &sock_addr))
			return nullptr;
		socket.type = SOCKET_TYPE_SERVER;
	}

	struct socket *place = sockdb_insert(sdbp, socket);

	if (place == nullptr)
		return nullptr;

	if (watch_socket(epfd, place, ios) == nullptr) {
		sockdb_close(sdbp, place);
		return nullptr;
	}

	return place;
}

enum _monitor_ev {
	MEV_CONTINUE,
	MEV_EXIT,
	MEV_EOF,
	MEV_ERROR,
};

private
enum _monitor_ev _monitor_sigfd(struct epoll_event * /*ev*/, int sigfd)
{
	struct signalfd_siginfo siginfo;
	while (read(sigfd, &siginfo, sizeof(siginfo)) > 0) {
		fprintf(stderr, "Received signal %d\n", siginfo.ssi_signo);

		switch (siginfo.ssi_signo) {
		case SIGTERM:
		case SIGINT:
			return MEV_EXIT;
		}
	}

	return MEV_CONTINUE;
}

private
enum _monitor_ev _monitor_server(int epfd, struct sockdb **sockdb,
		struct io **ios, struct socket *socket, struct epoll_event * /*ev*/)
{
	int fd;
	while ((fd = accept4(socket->fd, nullptr, nullptr, SOCK_NONBLOCK)) != -1) {
		struct socket tmp = {
			.fd = fd,
			.type = SOCKET_TYPE_CLIENT,
			.source = { .type = SOCKET_SOURCE_FD, .fd = socket->fd },
		};

		struct socket *client = sockdb_insert(sockdb, tmp);
		if (client == nullptr) {
			warn("[%2d] accept(%d): Cannot add socket to database", socket->fd, fd);
			close(fd);
			return MEV_CONTINUE;
		}

		if (watch_socket(epfd, client, ios) == nullptr) {
			warn("[%2d] accept(%d): Cannot setup socket watch", socket->fd, fd);
			sockdb_close(sockdb, client);
			return MEV_CONTINUE;
		}

		info("[%2d] Accepted new socket %d", socket->fd, fd);
	}

	return MEV_CONTINUE;
}

private
void _monitor_client_eof(int epfd, struct sockdb **sockdb, struct io ** /*ios*/,
		struct socket *socket)
{
	/* According to ‹man 7 epoll›, ‹close()›d descriptor should
	 * automatically be removed from the interest list. However, there
	 * are claims that this behaviour is buggy. */
	if (epoll_ctl(epfd, EPOLL_CTL_DEL, socket->fd, nullptr) != 0)
		warn("epoll_ctl(): Cannot remove %d\n", socket->fd);

	info("[%2d] Channel closed", socket->fd);
	sockdb_close(sockdb, socket);
}

private
enum _monitor_ev _monitor_client_accept_fd(struct sockdb **sdbp, struct socket *socket,
		struct cmsghdr *cmsg, int epfd, struct io **ios)
{
	int *fd = (int *) CMSG_DATA(cmsg);
	struct socket *new_channel = sockdb_insert(sdbp, (struct socket) {
		.fd = *fd,
		.type = SOCKET_TYPE_CLIENT,
		.source = { .type = SOCKET_SOURCE_FD, .fd = socket->fd },
	});

	if (new_channel == nullptr) {
		warnx("[%2d] Dropped new channel %d", socket->fd, *fd);
		close(*fd);
		return MEV_ERROR;
	}

	if (watch_socket(epfd, new_channel, ios) == nullptr) {
		warn("%d: Cannot setup new channel (%d) watch", socket->fd, *fd);
		sockdb_close(sdbp, new_channel);
		return MEV_ERROR;
	}

	info("[%2d] Passed new channel %d", socket->fd, *fd);
	return MEV_CONTINUE;
}

private
enum _monitor_ev _monitor_client_recv(int epfd, struct sockdb ** sdbp,
		struct io **ios, struct socket *socket)
{
	char message[4096], control[256];
	struct iovec iov[] = {
		{ .iov_base = message, .iov_len = sizeof(message) - 1 },
	};

	struct msghdr msg = {
		.msg_iov = iov,
		.msg_iovlen = array_size(iov),
		.msg_control = control,
		.msg_controllen = sizeof(control),
	};

	int recv;
	while ((recv = recvmsg(socket->fd, &msg, 0)) > 0) {
		message[recv] = '\0';
		printf("\x1b[36m[%2d]\x1b[0m %s", socket->fd, message);
		if (message[recv - 1] != '\n')
			putchar('\n');

		for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
				cmsg = CMSG_NXTHDR(&msg, cmsg)) {
			if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
				continue;

			_monitor_client_accept_fd(sdbp, socket, cmsg, epfd, ios);
		}
	}

	if (recv == 0)
		return MEV_EOF;

	if (errno == EWOULDBLOCK || errno == EAGAIN)
		return MEV_CONTINUE;

	warn("recvmsg(%d)", socket->fd);
	return MEV_ERROR;
}

private
enum _monitor_ev _monitor_client(int epfd, struct sockdb **sockdb,
		struct io **ios, struct socket *socket, struct epoll_event *ev)
{
	enum _monitor_ev sub = MEV_CONTINUE;
	if (ev->events & EPOLLIN)
		sub = _monitor_client_recv(epfd, sockdb, ios, socket);

	if (sub == MEV_EOF || sub == MEV_ERROR || ev->events & EPOLLHUP) {
		_monitor_client_eof(epfd, sockdb, ios, socket);
		sub = MEV_EOF;
	}

	return sub;
}

private
enum _monitor_ev _std_send(const struct socket *socket, const char *line, size_t line_len, int fd)
{
	if (socket == nullptr || socket->fd < 0) {
		fprintf(stderr, "No or invalid channel selected\n");
		return MEV_ERROR;
	}

	struct iovec iov[] = {
		/* Conversion safe: ‹sendmsg()› will never write into ‹iov›. */
		{ .iov_base = (char *) line, .iov_len = line_len },
	};

	union {
		char buf[CMSG_SPACE(sizeof(int[1]))];
		struct cmsghdr _align;
	} control;

	struct msghdr msg = {
		.msg_iov = iov,
		.msg_iovlen = array_size(iov),
		.msg_control = control.buf,
		.msg_controllen = sizeof(control.buf),
	};

	if (fd >= 0) {
		struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
		memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));
		msg.msg_controllen = cmsg->cmsg_len;
	} else {
		msg.msg_controllen = 0;
	}

	if (sendmsg(socket->fd, &msg, 0) == -1) {
		warn("sendmsg(%d)", socket->fd);
		info("Channel %d deselected; select a new one using /select N", socket->fd);
		return MEV_ERROR;
	}

	return MEV_CONTINUE;
}

private
bool _std_parse_cmd_as(struct cli_cmd *cmd, char *command, char *tokens[],
		struct cli_cmd_desc *cursor)
{
	for (size_t i = 0; i < cursor->required_tokens + 1; i++)
		tokens[i] = strtok(nullptr, i == cursor->required_tokens ? "\n" : " \n");

	if (cursor->required_tokens > 0 && tokens[cursor->required_tokens - 1] == nullptr) {
		info("%s: Not enough tokens; %zu required", command, cursor->required_tokens);
		return false;
	}

	if (!cursor->slurp_rest && tokens[cursor->required_tokens] != nullptr) {
		info("%s: Too many tokens; %zu required", command, cursor->required_tokens);
		return false;
	}

	if (cursor->parse != nullptr && !cursor->parse(cmd, tokens)) {
		info("%s: Cannot parse tokens", command);
		return false;
	}

	return true;
}

private
struct cli_cmd _std_parse_cmd(char *line, ssize_t /*line_len*/)
{
	assert(line != nullptr);
	assert(line[0] == '/');

	static const struct cli_cmd NOP = { .cmd_type = CLI_CMD_NOP };
	struct cli_cmd cmd = { };

	char *command = strtok(line, " \n");
	if (command == nullptr)
		return NOP;

	for (struct cli_cmd_desc *cursor = CLI_COMMANDS; cursor->command != nullptr; cursor++) {
		if (cursor->type == CLI_CMD_NOP)
			continue;

		if (cursor->type == CLI_CMD_DIRECT_MSG && !parse_int(&cmd.cmd_msg_via, &command[1]))
			continue;
		if (cursor->type != CLI_CMD_DIRECT_MSG && !streq(&command[1], cursor->command))
			continue;

		cmd.cmd_type = cursor->type;
		char **tokens = malloc((cursor->required_tokens + 1) * sizeof(char *));
		if (tokens == nullptr)
			return NOP;

		if (!_std_parse_cmd_as(&cmd, command, tokens, cursor)) {
			free(tokens);
			return NOP;
		}

		cmd._aux_data = tokens;
		return cmd;
	}

	info("%s: Unknown command", command);
	return NOP;
}

private
bool _std_cmd_direct_msg(struct cli_cmd *cmd, const struct sockdb *sockdb)
{
	const struct socket *target = sockdb_find(sockdb, cmd->cmd_msg_via);
	if (target == nullptr) {
		warnx("%d: No such channel", cmd->cmd_msg_via);
		return false;
	}

	return _std_send(target, cmd->cmd_msg_text, strlen(cmd->cmd_msg_text), -1) == MEV_CONTINUE;
}

private
const char *_std_cmd_list_type(enum socket_type type)
{
	static char buffer[256];
	socket_type_str3(sizeof(buffer), buffer, (struct socket_type_str_opt){
		.type = type,
		.flags = STSOF_NONE,
	});

	return buffer;
}

private
const char *_std_cmd_list_source(const struct socket_source *source)
{
	static char buffer[PATH_MAX];
	switch (source->type) {
	case SOCKET_SOURCE_FD:
		snprintf(buffer, sizeof(buffer), "channel %d", source->fd);
		break;
	case SOCKET_SOURCE_PATH:
		snprintf(buffer, sizeof(buffer), "path %s", source->path);
		break;
	default:
		snprintf(buffer, sizeof(buffer), "unknown source");
	}

	return buffer;
}

private
void _std_cmd_list(const struct sockdb *sockdb)
{
	for (size_t i = 0; i < sockdb->init; i++) {
		const struct socket *s = &sockdb->db[i];
		if (s->fd == -1)
			continue;

		printf("(%2d) %s from %s\n", s->fd, _std_cmd_list_type(s->type),
				_std_cmd_list_source(&s->source));
	}
}

private
bool _std_cmd_select(struct cli_cmd *cmd, const struct sockdb *sockdb,
		struct client *client)
{
	const struct socket *socket = sockdb_find(sockdb, cmd->cmd_fd);

	if (socket == nullptr)
		return warnx_v(false, "%d: No such channel", cmd->cmd_fd);

	if (socket->type == SOCKET_TYPE_SERVER)
		return warnx_v(false, "%d: Passive channel cannot be selected", socket->fd);

	client->selected = socket;
	return true;
}

private
bool _std_cmd_open(struct cli_cmd *cmd, struct sockdb **sdbp, struct client *client,
		int epfd, struct io **ios)
{
	const struct socket *tunnel = cmd->cmd_msg_via < 0
		? client->selected
		: sockdb_find_mut(sdbp, cmd->cmd_msg_via);

	if (tunnel == nullptr) {
		if (cmd->cmd_msg_via < 0)
			return warnx_v(false, "No channel selected");
		return warnx_v(false, "%d: No such channel opened", cmd->cmd_msg_via);
	}

	int sv[2];
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv) != 0)
		return warn_v(false, "socketpair()");

	/* sv[0] is to be kept.
	 * sv[1] is to be send away to the other side. */
	const char *msg = "\0";
	size_t msg_len = 1;

	if (cmd->cmd_msg_text != nullptr) {
		msg = cmd->cmd_msg_text;
		msg_len = strlen(cmd->cmd_msg_text);
	}

	if (_std_send(tunnel, msg, msg_len, sv[1]) == MEV_ERROR) {
		close(sv[0]);
		goto close_sv_1;
	}

	struct socket *new_client = sockdb_insert(sdbp, (struct socket){
		.fd = sv[0],
		.type = SOCKET_TYPE_CLIENT,
		.source = { .type = SOCKET_SOURCE_FD, .fd = tunnel->fd },
	});

	if (new_client == nullptr) {
		close(sv[0]);
		goto close_sv_1;
	}

	if (watch_socket(epfd, new_client, ios) == nullptr) {
		warn("%d: Cannot setup new socket watch", sv[0]);
		sockdb_close(sdbp, new_client);
		goto close_sv_1;
	}

	info("[%2d] Channel created", sv[0]);
	client->selected = new_client;

close_sv_1:
	close(sv[1]);
	return true;
}

private
bool _std_cmd_close(struct cli_cmd *cmd, struct sockdb **sdbp)
{
	struct socket *socket = sockdb_find_mut(sdbp, cmd->cmd_fd);

	if (socket == nullptr)
		return warnx_v(false, "%d: No such channel", cmd->cmd_fd);

	if (sockdb_close(sdbp, socket) != 0)
		return false;

	return true;
}

private
enum _monitor_ev _std_exec_cmd(struct cli_cmd *cmd, struct sockdb **sdbp, struct client *client,
		int epfd, struct io **ios)
{
	switch (cmd->cmd_type) {
	case CLI_CMD_NOP:
		break;
	case CLI_CMD_QUIT:
		return MEV_EXIT;
	case CLI_CMD_DIRECT_MSG:
		_std_cmd_direct_msg(cmd, *sdbp);
		break;
	case CLI_CMD_LIST:
		_std_cmd_list(*sdbp);
		break;
	case CLI_CMD_SELECT:
		_std_cmd_select(cmd, *sdbp, client);
		break;
	case CLI_CMD_OPEN:
		_std_cmd_open(cmd, sdbp, client, epfd, ios);
		break;
	case CLI_CMD_CLOSE:
		return _std_cmd_close(cmd, sdbp)
			? MEV_EOF : MEV_CONTINUE;
	default:
		info("Command not implemented");
	}

	return MEV_CONTINUE;
}

private inline
bool _std_line_is_empty(const char *str, ssize_t str_len_hint)
{
	if (str_len_hint < 0)
		str_len_hint = strlen(str);

	return (size_t) str_len_hint == strspn(str, " \t\n");
}

private
enum _monitor_ev _monitor_std(int epfd, struct sockdb **sdbp, struct io **ios,
		int fd, struct epoll_event *ev, struct client *client)
{
	enum _monitor_ev response = MEV_CONTINUE;

	if (ev->events & EPOLLIN) {
		char *line = nullptr;
		size_t line_len = 0;

		errno = 0;

		ssize_t rd;
		while ((rd = getline(&line, &line_len, stdin)) > 0) {
			if (line[0] != '/' || (rd >= 2 && line[1] == '/')) {
				if (_std_line_is_empty(line, rd))
					continue;

				if (_std_send(client->selected, line, rd, -1) == MEV_EOF)
					client->selected = nullptr;
			} else {
				struct cli_cmd cmd = _std_parse_cmd(line, rd);
				response = _std_exec_cmd(&cmd, sdbp, client, epfd, ios);
				free(cmd._aux_data);
			}
		}

		/* On EOF, trigger the next branch. */
		if (feof(stdin))
			ev->events |= EPOLLHUP;
		else if (ferror(stdin) && (errno == EWOULDBLOCK || errno == EAGAIN))
			clearerr(stdin);

		free(line);
	}

	if (ev->events & EPOLLHUP) {
		info("[%2d] Channel closed", fd);
		return MEV_EXIT;
	}

	return response;
}

private
enum _monitor_ev _monitor_ev_handle(int epfd, struct sockdb **sockdb,
		struct io **ios, struct client *client, struct epoll_event *ev)
{
	struct io *io = (struct io *) ev->data.ptr;

	switch (io->type) {
	case IO_SIGFD:
		return _monitor_sigfd(ev, io->sigfd);

	case IO_SOCKET:
		return io->socket->type == SOCKET_TYPE_SERVER
			? _monitor_server(epfd, sockdb, ios, io->socket, ev)
			: _monitor_client(epfd, sockdb, ios, io->socket, ev);

	case IO_STD:
		return _monitor_std(epfd, sockdb, ios, io->std, ev, client);

	default:
		bug("Unknown IO type %d", io->type);
	}

	return MEV_ERROR;
}

bool monitor(int epfd, struct sockdb **sockdb, struct io **ios, struct client *client)
{
	int count;
	struct epoll_event events[16];
	while ((count = epoll_wait(epfd, events, sizeof(events) / sizeof(events[0]), -1)) >= 0) {
		for (unsigned i = 0; i < (unsigned) count; i++) {
			switch (_monitor_ev_handle(epfd, sockdb, ios, client, &events[i])) {
			case MEV_EXIT:
				return true;
			case MEV_ERROR:
				return false;
			case MEV_EOF:
				if ((*sockdb)->size == 0) {
					info("No more channels, exiting");
					return true;
				}

				break;
			default:
				/* nop */ break;
			}
		}

		if (client->selected != nullptr && client->selected->fd < 0) {
			info("Deselecting closed channel, select a new one with /select N");
			client->selected = nullptr;
		}
	}

	return true;
}

int main(int argc, char *argv[])
{
	struct options options;
	options_process(&options, argc, argv);

	struct io *ios;
	io_chain_create(&ios);

	struct sockdb *sockdb;
	if (!sockdb_create(&sockdb))
		croak("Cannot setup socket collection");

	int epfd = epoll_create(1);
	if (epfd == -1)
		croak("epoll_create()");

	struct client client = { };
	struct socket *main_socket;
	if ((main_socket = sockets_init(&options, &sockdb, epfd, &ios)) == nullptr)
		croak("%s", options.path);

	if (options.mode == OP_CLIENT)
		client.selected = main_socket;

	if (watch_std(epfd, STDIN_FILENO, &ios) == nullptr)
		croak("Cannot setup input watch");

	struct io *io_sigfd;
	if ((io_sigfd = watch_signals(epfd, &ios)) == nullptr)
		croak("Cannot setup signal watch");

	bool status = monitor(epfd, &sockdb, &ios, &client);

	close(io_sigfd->sigfd);
	close(epfd);
	sockdb_destroy(&sockdb);

	io_chain_destroy(&ios);

	if (options.mode == OP_SERVER)
		unlink(options.path);

	return status ? EXIT_SUCCESS : EXIT_FAILURE;
}
