#pragma once
#ifndef SOCKDB_H
#define SOCKDB_H

#include <stddef.h>

#include "socket.h"

struct sockdb {
	size_t capacity; /* Total elements in db. */
	size_t size; /* Number of used elements. */
	size_t init; /* Number of initialised elements. */
	struct socket db[];
};

bool sockdb_create(struct sockdb **sdbp);
int sockdb_close(struct sockdb **sdbp, struct socket *socket);
void sockdb_destroy(struct sockdb **sdbp);
const struct socket *sockdb_find(const struct sockdb *sockdb, int fd);
struct socket *sockdb_find_mut(struct sockdb **sdbp, int fd);
struct socket *sockdb_insert(struct sockdb **sdbp, struct socket v);

#endif // SOCKDB_H
