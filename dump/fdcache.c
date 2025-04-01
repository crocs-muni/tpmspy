#include "fdcache.h"

#include <stdlib.h>
#include <string.h>

size_t _fd_cache_size(size_t fds)
{
	return sizeof(struct fd_cache) + sizeof(((struct fd_cache) { }).fdmap[0]) * fds;
}

struct fd_cache *fd_cache_new()
{
	struct fd_cache *cc = malloc(_fd_cache_size(0));

	if (cc == nullptr)
		return nullptr;

	cc->size = 0;
	return cc;
}

void fd_cache_destroy(struct fd_cache **pcc)
{
	free(*pcc);
	*pcc = nullptr;
}

bool _comm_cache_resize(struct fd_cache **pcc, size_t new_size)
{
	if ((*pcc)->size >= new_size)
		return true;

	struct fd_cache *new_cc = realloc(*pcc, _fd_cache_size(new_size));

	if (new_cc == nullptr)
		return false;

	*pcc = new_cc;

	memset(&(*pcc)->fdmap[(*pcc)->size], 0, sizeof(uint32_t) * (new_size - (*pcc)->size));
	(*pcc)->size = new_size;

	return true;
}

bool fd_cache_store(struct fd_cache **pcc, size_t ix, uint32_t v)
{
	if (ix >= (*pcc)->size && !_comm_cache_resize(pcc, ix + 1))
		return false;

	(*pcc)->fdmap[ix] = v;
	return true;
}

uint32_t fd_cache_load(struct fd_cache *cc, size_t ix)
{
	return cc->size >= ix ? cc->fdmap[ix] : 0;
}
