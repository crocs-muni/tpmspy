#include "sockdb.h"

#include <assert.h>
#include <stdlib.h>

#include <unistd.h>

#include "socket.h"
#include "minmax.h"

static inline
size_t _sockdb_size(size_t capacity)
{
	return sizeof(struct sockdb) + sizeof(struct socket) * capacity;
}

bool sockdb_create(struct sockdb **sdbp)
{
	assert(sdbp != NULL);

	return (*sdbp = calloc(1, _sockdb_size(0))) != NULL;
}

int sockdb_close(struct sockdb **sdbp, struct socket *socket)
{
	/* Ensure the socket is in this database. */
	assert(&(*sdbp)->db[0] <= socket && socket < &(*sdbp)->db[(*sdbp)->init]);

	if (socket->fd < 0)
		return -1;

	int ret = close(socket->fd);
	socket->fd = -1;
	(*sdbp)->size--;
	return ret;
}

static inline
void _sockdb_close_ix(struct sockdb **sdbp, size_t i)
{
	assert(i < (*sdbp)->init);
	sockdb_close(sdbp, &(*sdbp)->db[i]);
}

void sockdb_destroy(struct sockdb **sdbp)
{
	assert(sdbp != NULL);
	assert(*sdbp != NULL);

	for (size_t i = 0; i < (*sdbp)->init; i++)
		sockdb_close(sdbp, &(*sdbp)->db[i]);

	assert((*sdbp)->size == 0);

	free(*sdbp);
	*sdbp = NULL;
}

static
bool _sockdb_resize(struct sockdb **sdbp, size_t new_capacity)
{
	struct sockdb *new_db = realloc(*sdbp, _sockdb_size(new_capacity));

	if (new_db == NULL)
		return false;

	new_db->capacity = new_capacity;
	new_db->init = min(new_db->init, new_capacity);

	*sdbp = new_db;
	return true;
}

const struct socket *_sockdb_find_empty(const struct sockdb *sockdb)
{
	for (size_t i = 0; i < sockdb->init; i++) {
		if (sockdb->db[i].fd < 0)
			return &sockdb->db[i];
	}

	return NULL;
}

const struct socket *sockdb_find(const struct sockdb *sockdb, int fd)
{
	assert(sockdb != NULL);
	assert(fd >= 0);

	for (size_t i = 0; i < sockdb->init; i++) {
		if (sockdb->db[i].fd == fd)
			return &sockdb->db[i];
	}

	return NULL;
}

struct socket *sockdb_find_mut(struct sockdb **sdbp, int fd)
{
	/* This cast is safe, as we have a mutable pointer in the argument. */
	return (struct socket *) sockdb_find(*sdbp, fd);
}

struct socket *sockdb_insert(struct sockdb **sdbp, struct socket v)
{
	struct socket *place = (struct socket *) _sockdb_find_empty(*sdbp);

	if (place == NULL) {
		if ((*sdbp)->init >= (*sdbp)->capacity
				&& !_sockdb_resize(sdbp, (*sdbp)->capacity + 16))
			return NULL;

		assert((*sdbp)->init < (*sdbp)->capacity);
		place = &(*sdbp)->db[(*sdbp)->init];

		(*sdbp)->init++;
	}

	(*sdbp)->size++;
	*place = v;
	return place;
}
