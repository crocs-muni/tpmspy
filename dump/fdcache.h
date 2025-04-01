#pragma once
#ifndef FD_CACHE_H
#define FD_CACHE_H

#include <stddef.h>
#include <stdint.h>

struct fd_cache {
	size_t size;
	uint32_t fdmap[];
};

struct fd_cache *fd_cache_new(void);
void fd_cache_destroy(struct fd_cache **pcc);
bool fd_cache_store(struct fd_cache **pcc, size_t ix, uint32_t v);
uint32_t fd_cache_load(struct fd_cache *cc, size_t ix);

#endif // FD_CACHE_H
