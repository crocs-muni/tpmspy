/*
 * swtpm — An interceptor utility that spawns real ‹swtpm› and ‹sockspy›
 *
 * ‹virt-manager› usually starts ‹swtpm› with ‹exec*p()› call, which in
 * turn resolves to ‹/usr/bin/swtpm›. If we have ‹/usr/local/bin› in ‹$PATH›
 * *before* ‹/usr/bin›, we can easily intercept the binary.
 *
 * ‹virt-manager› starts ‹swtpm› multiple times, mostly as
 *
 *   $ swtpm socket --print-capabilities
 *   $ swtpm socket --print-capabilities --tpm2
 *   $ swtpm socket --ctrl type=unixio,path=$SOCK --tpmstate dir=$DIR ...
 *
 * By default, all commands should be passed to ‹/usr/bin/swtpm›.
 * The ‹socket --ctrl type=unixio,path=$SOCK …› should be diverged, however,
 * with putting ‹sockspy›'s own socket in the argument before passing it
 * to real ‹swtpm›. */

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <unistd.h>

#include <libgen.h>
#include <sys/inotify.h>
#include <sys/syscall.h>
#include <sys/signalfd.h>
#include <sys/wait.h>

#include "defs.h"
#include "msg.h"
#include "utils.h"

static
const char SWTPM_BIN[] = "/usr/bin/swtpm";

static
const char SOCKSPY_BIN[] = "/usr/local/bin/sockspy";

static
const int POLL_TIMEOUT = 2000;

static
bool find_unixio_path(int argc, char *argv[], int *unixio_arg)
{
	for (int i = 1; i < argc; i++) {
		if (streq(argv[i], "--ctrl") && strstr(argv[i + 1], "unixio") != NULL
				&& strstr(argv[i + 1], "path") != NULL) {
			*unixio_arg = i + 1;
			return true;
		}
	}

	return false;
}

static
void _passthrough_check_exe_loop(void)
{
	char exe[PATH_MAX];
	ssize_t exe_size;
	if ((exe_size = readlink("/proc/self/exe", exe, sizeof(exe))) < 0
			|| exe_size == sizeof(exe))
		croak("readlink(): Truncation occurred");

	/* ‹readlink()› does not append the terminating byte. */
	exe[exe_size] = '\0';

	if (streq(exe, SWTPM_BIN))
		croak("passthrough(): Loop detected for execv()");
}

static noreturn
void passthrough(unused int argc, char *argv[])
{
	/* Prevent unwanted loops if, by accident, we aready are run as the
	 * target binary. */
	_passthrough_check_exe_loop();

	execv(SWTPM_BIN, argv);
	croak("exec(%s)", SWTPM_BIN);
}

static
char *extract_socket_path(const char *arg)
{
	char *path = strstr(arg, "path=");
	if (path == NULL)
		return warnx_v(NULL, "Cannot extract socket path: Socket path not found");

	path += strlen("path=");

	char *copy = strdup(path);
	if (copy == NULL)
		return warn_v(NULL, "Cannot extract socket path: strdup()");

	char *comma = strchr(copy, ',');
	if (comma != NULL)
		*comma = '\0';

	return copy;
}

static
bool replace_swtpm_socket(char **arg)
{
	/* ‹strlen(*arg)› definitely has enough space for "path=". We only
	 * intend to change socket name, so in the worst case, we need to
	 * add ‹NAME_MAX› bytes. */
	size_t buffer_size = strlen(*arg) + NAME_MAX;
	char *buffer = malloc(buffer_size);

	if (buffer == NULL)
		return warn_v(false, "Cannot intercept socket: malloc()");

	size_t cursor = 0;
	char *token = *arg;

	/* Start with an empty string so we can use ‹strlcat()› everywhere. */
	buffer[0] = '\0';

	while ((token = strtok(cursor == 0 ? token : NULL, ",")) != NULL) {
		if (cursor != 0)
			strlcat(&buffer[cursor], ",", buffer_size - cursor);

		if (strncmp(token, "path=", strlen("path=")) == 0) {
			/* Virt-Manager may choose to hold multiple sockets in
			 * the target directory. Choosing a truly random name
			 * will unlikely cause conflicts, but why take chances.
			 * We will insert ‹gate› between the name and ‹.sock›. */
			char *original = token + strlen("path=");
			char *slash = strrchr(original, '/');
			char *suffix = strstr(original, ".sock");

			strlcat(&buffer[cursor], "path=", buffer_size - cursor);

			// Append original path
			if (slash != NULL) {
				*slash = '\0';
				strlcat(&buffer[cursor], original, buffer_size - cursor);
				strlcat(&buffer[cursor], "/", buffer_size - cursor);
			}

			if (suffix != NULL)
				*suffix = '\0';

			// Append original socket name
			strlcat(&buffer[cursor], &slash[1], buffer_size - cursor);

			// Append suffixes
			strlcat(&buffer[cursor], ".gate.sock", buffer_size - cursor);
		} else {
			strlcat(&buffer[cursor], token, buffer_size - cursor);
		}

		cursor += strlen(buffer + cursor);
		assert(cursor <= buffer_size);
	}

	*arg = buffer;
	return true;
}

static
int pidfd_open(pid_t pid, int flags)
{
	return syscall(SYS_pidfd_open, pid, flags);
}

static
int pidfd_send_signal(int pidfd, int sig, siginfo_t *info, unsigned int flags)
{
	return syscall(SYS_pidfd_send_signal, pidfd, sig, info, flags);
}

static
bool _open_to(const char *path, int flags, int target_fd)
{
	bool ok = false;

	int fd = open(path, flags);
	if (fd == -1)
		warn_jmp(leave, "open(%s)", path);

	if (dup2(fd, target_fd) == -1)
		warn_jmp(close_fd, "dup2()");

	ok = true;

close_fd:
	close(fd);
leave:
	return ok;
}

static noreturn
void _start_swtpm_exec(char *argv[])
{
	char log_file[PATH_MAX] = "/var/tmp/sockspy_swtpm.XXXXXX.log";
	int log_fd = mkstemps(log_file, strlen(".log"));

	if (log_fd == -1)
		croak("Cannot exec swtpm: mkstemps(%s)", log_file);

	/* Redirect stdin. */
	if (!_open_to("/dev/null", O_RDONLY, STDIN_FILENO)
			|| dup2(log_fd, STDOUT_FILENO) == -1
			|| dup2(log_fd, STDERR_FILENO) == -1)
		croak("Cannot exec swtpm: I/O redirection failed");

	/* The child process executes ‹swtpm›. */
	argv[0] = (char *) SWTPM_BIN;
	execv(SWTPM_BIN, argv);
	croak("Cannot exec swtpm: exec(%s)", SWTPM_BIN);
}

enum _start_swtpm_state {
	_start_swtpm_continue,
	_start_swtpm_error,
	_start_swtpm_ok,
};

static
enum _start_swtpm_state _start_swtpm_file_created(int inofd, const char *path)
{
	char evbuf[sizeof(struct inotify_event) + NAME_MAX + 1];

	ssize_t rd;
	if ((rd = read(inofd, evbuf, sizeof(evbuf))) == -1)
		return warn_v(_start_swtpm_error, "read()");

	struct inotify_event *ev;
	for (ssize_t cursor = 0; cursor < rd; cursor += sizeof(*ev) + ev->len) {
		ev = (struct inotify_event *) &evbuf[cursor];

		if (ev->mask & IN_CREATE && streq(ev->name, path))
			return _start_swtpm_ok;
	}

	return _start_swtpm_continue;
}

static
bool _start_swtpm_await_socket(int inofd, int pidfd, const char *socket_path)
{
	struct pollfd fds[] = {
		{ .fd = inofd, .events = POLLIN },
		{ .fd = pidfd, .events = POLLIN },
	};

	const int nfds = sizeof(fds) / sizeof(fds[0]);

	enum _start_swtpm_state loop_state = _start_swtpm_continue;
	while (loop_state == _start_swtpm_continue && poll(fds, nfds, POLL_TIMEOUT) != -1) {
		for (int i = 0; i < nfds; i++) {
			if (fds[i].fd == pidfd && fds[i].revents & POLLIN) {
				/* pidfd is readable, swtpm exited */
				loop_state = _start_swtpm_error;
			} else if (fds[i].fd == inofd && fds[i].revents & POLLIN) {
				switch (loop_state = _start_swtpm_file_created(inofd, socket_path)) {
				case _start_swtpm_error:
				case _start_swtpm_ok:
					goto leave;

				case _start_swtpm_continue:
					break;
				}
			}
		}
	}

leave:
	return loop_state == _start_swtpm_ok;
}

static
pid_t start_swtpm(unused int argc, char *argv[], const char *sock)
{
	bool status = false;

	char *sock_copy = strdup(sock);
	if (sock_copy == NULL)
		return warn_v(-1, "Cannot start swtpm: strdup()");

	const char *sock_name = basename(sock_copy);
	const char *sock_dir = dirname(sock_copy);

	/* We will need to wait for a socket to appear. */
	int inofd = inotify_init1(IN_CLOEXEC);
	if (inofd == -1)
		warn_jmp(cleanup_sock_name, "Cannot start swtpm: inotify_init()");

	if (inotify_add_watch(inofd, sock_dir, IN_CREATE) == -1)
		warn_jmp(cleanup_inotify, "Cannot start swtpm: inotify_add_watch()");

	pid_t pid = fork();

	if (pid == -1)
		return warn_v(-1, "Cannot start swtpm: fork()");

	if (pid == 0)
		/* noreturn */ _start_swtpm_exec(argv);

	/* Parent process either waits for the child to die, or socket to  appear. */
	int pidfd = pidfd_open(pid, 0);
	if (pidfd == -1)
		warn_jmp(cleanup_kill, "Cannot start swtpm: pidfd_open()");

	status = _start_swtpm_await_socket(inofd, pidfd, sock_name);

	close(pidfd);

cleanup_kill:
	/* Best effort — if this fails, nothing can be done anyway. */
	if (pid != -1 && !status)
		kill(pid, SIGKILL);

cleanup_inotify:
	close(inofd);

cleanup_sock_name:
	free(sock_copy);

	return status ? pid : -1;
}

static noreturn
void _start_sockspy_exec(const char *swtpm_sock, const char *qemu_sock)
{
	char dump_file[PATH_MAX] = "/var/tmp/sockspy_dump.XXXXXX.bin";
	int dump_fd = mkstemps(dump_file, strlen(".bin"));

	if (dump_fd == -1)
		croak("Cannot exec sockspy: mkstemps(%s)", dump_file);

	char log_file[PATH_MAX] = "/var/tmp/sockspy.XXXXXX.log";
	int log_fd = mkstemps(log_file, strlen(".log"));

	if (log_fd == -1)
		croak("Cannot exec sockspy: mkstemps(%s)", log_file);

	/* Redirect stdin. */
	if (!_open_to("/dev/null", O_RDONLY, STDIN_FILENO)
			|| dup2(log_fd, STDOUT_FILENO) == -1
			|| dup2(log_fd, STDERR_FILENO) == -1)
		croak("Cannot exec sockspy: I/O redirection failed");

	char *args[] = {
		(char *) SOCKSPY_BIN,
		"--dump-file", dump_file,
		(char *) swtpm_sock,
		(char *) qemu_sock,
		NULL,
	};

	close(dump_fd);
	close(log_fd);

	execv(SOCKSPY_BIN, args);
	croak("Cannot exec sockspy: exec(%s)", SOCKSPY_BIN);
}

static
int start_sockspy(char *swtpm_sock, char *qemu_sock)
{
	pid_t pid = fork();

	if (pid == -1)
		return warn_v(-1, "Cannot start sockspy: fork()");

	if (pid == 0)
		/* noreturn */ _start_sockspy_exec(swtpm_sock, qemu_sock);

	return pid;
}

static
void _monitor_kill_all(size_t fds_count, struct pollfd fds[fds_count], int sig)
{
	for (size_t i = 0; i < fds_count; i++) {
		if (fds[i].fd != -1) {
			/* Best effort: We do not mind errors here. */
			pidfd_send_signal(fds[i].fd, sig, NULL, 0);
		}
	}
}

static
void _monitor_handle_child(pid_t pid)
{
	int wstatus;
	waitpid(pid, &wstatus, 0);

	if (WIFEXITED(wstatus))
		printf("%d: Exited (%d)\n", pid, WEXITSTATUS(wstatus));
	else
		printf("%d: Died (%d)\n", pid, WTERMSIG(wstatus));
}

static
int _monitor_handle_signal(int signalfd, size_t pid_count, struct pollfd fds[pid_count + 1])
{
	struct signalfd_siginfo info;

	while (read(signalfd, &info, sizeof(info)) > 0)
		_monitor_kill_all(pid_count, fds, info.ssi_signo);

	return info.ssi_signo;
}

static
bool _monitor_loop(size_t pid_count, pid_t pids[pid_count], struct pollfd fds[pid_count + 1])
{
	bool status = true;

	size_t children = pid_count;
	while (children > 0 && poll(fds, pid_count + 1, -1) != -1) {
		for (size_t i = 0; i < pid_count + 1; i++) {
			if (fds[i].fd == -1)
				continue;

			if (fds[i].revents & POLLIN) {
				if (i == pid_count) {
					int sig = _monitor_handle_signal(fds[pid_count].fd, pid_count, fds);
					status = sig == SIGTERM;
				} else {
					_monitor_handle_child(pids[i]);

					children--;
					close(fds[i].fd);
					fds[i].fd = -1;
				}
			}
		}
	}

	return status;
}

static
bool monitor(size_t pid_count, pid_t pids[pid_count])
{
	sigset_t signals, previous;

	sigemptyset(&signals);
	sigaddset(&signals, SIGTERM);
	sigaddset(&signals, SIGINT);

	/* Mask the signals to prevent their usual signal disposition. */
	if (sigprocmask(SIG_BLOCK, &signals, &previous) == -1)
		return warn_v(false, "monitor: sigprocmask()");

	bool status = false;

	struct pollfd *fds = calloc(pid_count + 1, sizeof(struct pollfd));

	if (fds == NULL)
		warn_jmp(restore_sigmask, "monitor: calloc()");

	for (size_t i = 0; i < pid_count; i++) {
		if ((fds[i].fd = pidfd_open(pids[i], 0)) == -1)
			warn_jmp(release_fds, "monitor: pidfd_open()");

		fds[i].events = POLLIN;
	}

	if ((fds[pid_count].fd = signalfd(-1, &signals, SFD_NONBLOCK)) == -1)
		warn_jmp(release_fds, "monitor: signalfd()");

	fds[pid_count].events = POLLIN;

	status = _monitor_loop(pid_count, pids, fds);

release_fds:
	for (size_t i = 0; i < pid_count + 1; i++) {
		if (fds[i].fd > 0) /* We used ‹calloc› */
			close(fds[i].fd);
	}

	free(fds);

restore_sigmask:
	sigprocmask(SIG_SETMASK, &previous, NULL);
	return status;
}

int main(int argc, char *argv[])
{
	int unixio_arg;
	if (!find_unixio_path(argc, argv, &unixio_arg))
		/* noreturn */ passthrough(argc, argv);

	errno = 0;
	char *qemu_sock = extract_socket_path(argv[unixio_arg]);
	if (qemu_sock == NULL)
		die("Cannot extract socket path from \"%s\"", argv[unixio_arg]);

	/* 1: Run ‹swtpm› with changed ‹argv[unixio_arg]› pointing to
	 *    a different socket. */
	if (!replace_swtpm_socket(&argv[unixio_arg]))
		die("Cannot replace arguments for swtpm");

	/* ‹qemu_sock› is the original path, extract the new one. */
	char *swtpm_sock = extract_socket_path(argv[unixio_arg]);
	pid_t swtpm_pid = start_swtpm(argc, argv, swtpm_sock);

	if (swtpm_pid == -1)
		warn_jmp(cleanup_paths, "Cannot start swtpm");

	/* 2: Start ‹sockspy› to bridge (modified) ‹swtpm_sock› now handled
	 *    by ‹swtpm›, and the original ‹qemu_sock› expected by QEMU. */
	pid_t sockspy_pid = start_sockspy(swtpm_sock, qemu_sock);
	if (sockspy_pid == -1) {
		kill(swtpm_pid, SIGTERM);
		goto cleanup_paths;
	}

	/* 3: Wait for both processes to exit. On SIGINT or SIGTERM, relay
	 *    the signals to both processes. */
	monitor(2, (pid_t[]){ swtpm_pid, sockspy_pid, -1 });

cleanup_paths:
	free(argv[unixio_arg]);
	free(swtpm_sock);
	free(qemu_sock);

	return 0;
}
