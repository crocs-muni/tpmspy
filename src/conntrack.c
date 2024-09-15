#include "conntrack.h"

#include <assert.h>

#include <defs.h>

#define assert_instance(X) \
	do { \
		assert((X) != NULL); \
		assert(*(X) != NULL); \
	} while (0)

#define assert_entry_in_array(CT, E) \
	assert(&(CT)->entries[0] <= (E) && (E) < &(CT)->entries[(CT)->size])

#define EMPTY_ENTRY \
	((struct conntrack_entry){ .a = -1, .b = -1 })

private inline
size_t _conntrack_size(size_t capacity)
{
	return sizeof(struct conntrack) + sizeof(struct conntrack_entry) * capacity;
}

bool conntrack_init(struct conntrack **ctp)
{
	assert(ctp != NULL);
	return (*ctp = calloc(1, _conntrack_size(0))) != NULL;
}

void conntrack_destroy(struct conntrack **ctp)
{
	assert_instance(ctp);

	free(*ctp);
	*ctp = NULL;
}

private
bool _conntrack_resize(struct conntrack **ctp, size_t new_capacity)
{
	struct conntrack *new_ct = realloc(*ctp, _conntrack_size(new_capacity));

	if (new_ct == NULL)
		return false;

	new_ct->capacity = new_capacity;
	*ctp = new_ct;
	return true;
}

private inline
bool _conntrack_is_unused(const struct conntrack_entry *ce)
{
	return ce->a <= -1 && ce->b <= -1;
}

bool conntrack_add(struct conntrack **ctp, struct conntrack_entry entry)
{
	assert_instance(ctp);

	assert(entry.a >= 0);
	assert(entry.b >= 0);

	for (size_t i = 0; i < (*ctp)->size; i++) {
		struct conntrack_entry *cursor = &(*ctp)->entries[i];

		if (_conntrack_is_unused(cursor)) {
			*cursor = entry;
			return true;
		}
	}

	if ((*ctp)->size == (*ctp)->capacity && !_conntrack_resize(ctp, (*ctp)->capacity + 16))
		return false;

	(*ctp)->entries[(*ctp)->size] = entry;
	(*ctp)->size++;
	return true;
}

private
void _conntrack_gc(struct conntrack **ctp)
{
	struct conntrack *ct = *ctp;
	const struct conntrack_entry *cursor;

	while (ct->size > 0 && (cursor = &ct->entries[ct->size - 1], true)
			&& _conntrack_is_unused(cursor))
		ct->size--;
}

bool conntrack_remove(struct conntrack **ctp, struct conntrack_entry *entry)
{
	assert_instance(ctp);
	assert(entry != NULL);

	assert_entry_in_array(*ctp, entry);

	entry->a = -1;
	entry->b = -1;

	_conntrack_gc(ctp);
	return true;
}

size_t conntrack_disconnect(struct conntrack **ctp, int fd)
{
	assert_instance(ctp);

	struct conntrack *ct = *ctp;

	size_t disconnected = 0;
	for (size_t i = 0; i < ct->size; i++) {
		struct conntrack_entry *entry = &ct->entries[i];
		if (entry->a == fd || entry->b == fd) {
			*entry = EMPTY_ENTRY;
			disconnected++;
		}
	}

	if (disconnected > 0)
		_conntrack_gc(ctp);

	return disconnected;
}

const struct conntrack_entry *conntrack_find(const struct conntrack *ct,
		const struct conntrack_entry *prev, int fd)
{
	assert(ct != NULL);

	if (prev == NULL)
		prev = &ct->entries[-1];
	else
		assert_entry_in_array(ct, prev);

	for (const struct conntrack_entry *cursor = prev + 1;
			cursor < &ct->entries[ct->size];
			cursor++) {
		if (cursor->a == fd || cursor->b == fd)
			return cursor;
	}

	return NULL;
}

struct conntrack_entry *conntrack_find_mut(struct conntrack **ctp, struct conntrack_entry *prev,
		int fd)
{
	return (struct conntrack_entry *) conntrack_find(*ctp, prev, fd);
}

int conntrack_find_fd(const struct conntrack *ct,
		const struct conntrack_entry *prev, int fd)
{
	const struct conntrack_entry *conn = conntrack_find(ct, prev, fd);

	if (conn == NULL)
		return -1;

	return conn->a == fd ? conn->b : conn->a;
}

