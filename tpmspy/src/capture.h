#pragma once
#ifndef CAPTURE_H
#define CAPTURE_H

#include <stddef.h>
#include <sys/uio.h>

#include "sockspy.h"

[[nodiscard]]
struct io *tpm_capture_open(struct context *ctx, size_t n);

void tpm_capture_close(struct io *io);
bool tpm_capture_write(struct socket_watcher *link[2], struct timeval *stamp,
		struct socket_watcher *new_link[2], struct iovec *payload);

#endif // CAPTURE_H
