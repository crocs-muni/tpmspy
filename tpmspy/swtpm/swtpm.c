/*
 * swtpm — An interceptor utility that spawns real ‹swtpm› and ‹tpmspy›
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
 * with putting ‹tpmspy›'s own socket in the argument before passing it
 * to real ‹swtpm›. */

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <unistd.h>

#include <libgen.h>
#include <sys/inotify.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "defs.h"
#include "msg.h"
#include "utils.h"

private
const char SWTPM_BIN[] = "/usr/bin/swtpm";

private
const char TPMSPY_BIN[] = "/usr/local/bin/tpmspy";

private
const int POLL_TIMEOUT = 2000;

private
bool find_unixio_path(int argc, char *argv[], int *unixio_arg)
{
	for (int i = 1; i < argc; i++) {
		if (strncmp(argv[i], "--ctrl", strlen("--ctrl")) != 0)
			continue;

		/* The '--OPTION=ARGUMENT' form. */
		if (strstr(argv[i], "unixio") != nullptr
				&& strstr(argv[i], "path") != nullptr) {
			*unixio_arg = i + 1;
			return true;
		}

		/* The '--OPTION ARGUMENT' form. */
		if (argv[i + 1] != nullptr && strstr(argv[i + 1], "unixio") != nullptr
				&& strstr(argv[i + 1], "path") != nullptr) {
			*unixio_arg = i + 1;
			return true;
		}
	}

	return false;
}

private
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

no_return private
void passthrough(int /*argc*/, char *argv[])
{
	/* Prevent unwanted loops if, by accident, we already are run as the
	 * target binary. */
	_passthrough_check_exe_loop();

	execv(SWTPM_BIN, argv);
	croak("exec(%s)", SWTPM_BIN);
}

private
char *extract_socket_path(const char *arg)
{
	char *path = strstr(arg, "path=");
	if (path == nullptr)
		return warnx_v(nullptr, "Cannot extract socket path: Socket path not found");

	path += strlen("path=");

	char *copy = strdup(path);
	if (copy == nullptr)
		return warn_v(nullptr, "Cannot extract socket path: strdup()");

	char *comma = strchr(copy, ',');
	if (comma != nullptr)
		*comma = '\0';

	return copy;
}

private
bool replace_swtpm_socket(const char *data_dir, char **arg)
{
	/* ‹strlen(*arg)› definitely has enough space for "path=". We only
	 * intend to change socket name, so in the worst case, we need to
	 * add ‹NAME_MAX› bytes. */
	size_t buffer_size = strlen(*arg) + NAME_MAX;
	char *buffer = malloc(buffer_size);

	if (buffer == nullptr)
		return warn_v(false, "Cannot intercept socket: malloc()");

	size_t cursor = 0;
	char *token = *arg;

	/* Start with an empty string so we can use ‹strlcat()› everywhere. */
	buffer[0] = '\0';

	while ((token = strtok(cursor == 0 ? token : nullptr, ",")) != nullptr) {
		if (cursor != 0)
			strlcat(&buffer[cursor], ",", buffer_size - cursor);

		if (strncmp(token, "path=", strlen("path=")) == 0) {
			char *original = token + strlen("path=");
			char *slash = strrchr(original, '/');

			strlcat(&buffer[cursor], "path=", buffer_size - cursor);

			/* Append temporary path and a slash. */
			strlcat(&buffer[cursor], data_dir, buffer_size - cursor);
			strlcat(&buffer[cursor], "/", buffer_size - cursor);

			/* Append the file name. */
			if (slash != nullptr)
				strlcat(&buffer[cursor], &slash[1], buffer_size - cursor);
			else
				strlcat(&buffer[cursor], original, buffer_size - cursor);
		} else {
			strlcat(&buffer[cursor], token, buffer_size - cursor);
		}

		cursor += strlen(buffer + cursor);
		assert(cursor <= buffer_size);
	}

	*arg = buffer;
	return true;
}

private
int pidfd_open(pid_t pid, int flags)
{
	return syscall(SYS_pidfd_open, pid, flags);
}

private
int pidfd_send_signal(int pidfd, int sig, siginfo_t *info, unsigned int flags)
{
	return syscall(SYS_pidfd_send_signal, pidfd, sig, info, flags);
}

private
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

private inline
int creat_excl(const char *filename, int flags)
{
	return open(filename, O_RDWR | O_CREAT | O_EXCL, flags);
}

no_return private
void _start_swtpm_exec(const char *data_dir, char *argv[])
{
	char log_file[PATH_MAX];
	if (snprintf(log_file, sizeof(log_file), "%s/%s", data_dir, "swtpm.log") >= (int) sizeof(log_file))
		croak("Log file path too long");

	int log_fd = creat_excl(log_file, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

	if (log_fd == -1)
		croak("Cannot exec swtpm: open(%s)", log_file);

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

private
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

private
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

private
pid_t start_swtpm(int /*argc*/, char *argv[], const char *data_dir, const char *sock)
{
	bool status = false;

	char *sock_copy = strdup(sock);
	if (sock_copy == nullptr)
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
		/* noreturn */ _start_swtpm_exec(data_dir, argv);

	/* Parent process either waits for the child to die, or socket to appear. */
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

no_return private
void _start_tpmspy_exec(const char *data_dir, const char *swtpm_sock, const char *qemu_sock)
{
	char log_file[PATH_MAX];

	char *capture_arg = nullptr;
	if (asprintf(&capture_arg, "capture:%s/packets-%%02x.bin", data_dir) == -1)
		croak("asprintf()");

	if (snprintf(log_file, sizeof(log_file), "%s/tpmspy.log", data_dir) >= (int) sizeof(log_file))
		croak("TPMSpy log path too long");

	int log_fd = creat_excl(log_file, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

	if (log_fd == -1)
		croak("Cannot exec tpmspy: open(%s)", log_file);

	/* Redirect stdin. */
	if (!_open_to("/dev/null", O_RDONLY, STDIN_FILENO)
			|| dup2(log_fd, STDOUT_FILENO) == -1
			|| dup2(log_fd, STDERR_FILENO) == -1)
		croak("Cannot exec tpmspy: I/O redirection failed");

	if (setenv("LSAN_OPTIONS", "verbosity=1:log_threads=1", 1) != 0)
		warn("_start_tpmspy_exec(): setenv()");

	char *args[] = {
		(char *) TPMSPY_BIN,
		"--sink", capture_arg,
		(char *) swtpm_sock,
		(char *) qemu_sock,
		nullptr,
	};

	close(log_fd);

	execv(TPMSPY_BIN, args);
	croak("Cannot exec tpmspy: exec(%s)", TPMSPY_BIN);
}

private
int start_tpmspy(const char *data_dir, char *swtpm_sock, char *qemu_sock)
{
	pid_t pid = fork();

	if (pid == -1)
		return warn_v(-1, "Cannot start tpmspy: fork()");

	if (pid == 0)
		/* noreturn */ _start_tpmspy_exec(data_dir, swtpm_sock, qemu_sock);

	return pid;
}

private
void _monitor_kill_all(size_t fds_count, struct pollfd fds[fds_count], int sig)
{
	for (size_t i = 0; i < fds_count; i++) {
		if (fds[i].fd != -1) {
			/* Best effort: We do not mind errors here. */
			pidfd_send_signal(fds[i].fd, sig, nullptr, 0);
		}
	}
}

private
void _monitor_handle_child(pid_t pid)
{
	int wstatus;
	waitpid(pid, &wstatus, 0);

	if (WIFEXITED(wstatus))
		printf("%d: Exited (%d)\n", pid, WEXITSTATUS(wstatus));
	else
		printf("%d: Died (%d)\n", pid, WTERMSIG(wstatus));
}

private
int _monitor_handle_signal(int signalfd, size_t pid_count, struct pollfd fds[pid_count + 1])
{
	struct signalfd_siginfo info;

	while (read(signalfd, &info, sizeof(info)) > 0)
		_monitor_kill_all(pid_count, fds, info.ssi_signo);

	return info.ssi_signo;
}

private
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

private
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

	if (fds == nullptr)
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
	sigprocmask(SIG_SETMASK, &previous, nullptr);
	return status;
}

private
bool _create_data_dir(size_t path_size, char path[path_size])
{
	time_t epoch = time(nullptr);

	struct tm tm;
	if (localtime_r(&epoch, &tm) == nullptr)
		croak("localtime_r()");

	if (strftime(path, path_size, "/var/tmp/tpmspy-%Y%m%d-%H%M-XXXXXX", &tm) == 0)
		croak("strftime()");

	if (mkdtemp(path) == nullptr)
		return warn_v(false, "mkdtemp()");

	if (chmod(path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0)
		warn("chmod()");

	return true;
}

#if !defined(GNU_SHENANIGANS) || GNU_SHENANIGANS
/* It is usually not a great idea to change GNU's internals. However, we are
 * to execute the program with the same name in different directory, so for
 * the sake of log clarity, we will change program name. */
extern char *__progname;
#endif

int main(int argc, char *argv[])
{
#if !defined(GNU_SHENANIGANS) || GNU_SHENANIGANS
	/* Change program name. */
	__progname = "swtpm[fake]";
#endif

	int unixio_arg;
	if (!find_unixio_path(argc, argv, &unixio_arg))
		/* noreturn */ passthrough(argc, argv);

	errno = 0;
	char *qemu_sock = extract_socket_path(argv[unixio_arg]);
	if (qemu_sock == nullptr)
		die("Cannot extract socket path from \"%s\"", argv[unixio_arg]);

	/* Create a temporary directory for outputs. */
	static char data_dir[PATH_MAX];
	if (!_create_data_dir(sizeof(data_dir), data_dir))
		die("Cannot create data directory");

	/* 1: Run ‹swtpm› with changed ‹argv[unixio_arg]› pointing to
	 *    a different socket. */
	if (!replace_swtpm_socket(data_dir, &argv[unixio_arg]))
		die("Cannot replace arguments for swtpm");

	/* ‹qemu_sock› is the original path, extract the new one. */
	char *swtpm_sock = extract_socket_path(argv[unixio_arg]);
	pid_t swtpm_pid = start_swtpm(argc, argv, data_dir, swtpm_sock);

	if (swtpm_pid == -1)
		warn_jmp(cleanup_paths, "Cannot start swtpm");

	/* 2: Start ‹tpmspy› to bridge (modified) ‹swtpm_sock› now handled
	 *    by ‹swtpm›, and the original ‹qemu_sock› expected by QEMU. */
	pid_t tpmspy_pid = start_tpmspy(data_dir, swtpm_sock, qemu_sock);
	if (tpmspy_pid == -1) {
		kill(swtpm_pid, SIGTERM);
		goto cleanup_paths;
	}

	/* 3: Wait for both processes to exit. On SIGINT or SIGTERM, relay
	 *    the signals to both processes. */
	monitor(2, (pid_t[]){ swtpm_pid, tpmspy_pid, -1 });

cleanup_paths:
	free(argv[unixio_arg]);
	free(swtpm_sock);
	free(qemu_sock);

	return 0;
}
