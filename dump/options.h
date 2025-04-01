#pragma once
#ifndef OPTIONS_H
#define OPTIONS_H

#include "fdcache.h"

struct options {
	bool json;
	const char *path;
};

struct resources {
	struct fd_cache *fdc;
};

void options_process(struct options *options, int argc, char *argv[]);

#endif // OPTIONS_H
