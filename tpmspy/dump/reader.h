#pragma once
#ifndef READER_H
#define READER_H

#include <dump.h>

#include "options.h"

enum dump_reader_status {
	DR_STATUS_ERRNO = -1,
	DR_STATUS_EOF   =  0,
	DR_STATUS_OK    =  1,
};

struct dump_reader {
	void *(*create)(struct resources *fdc);
	bool  (*destroy)(void *);

	enum dump_reader_status (*read)(void *, int, struct dump_packet **, size_t *);
};

const struct dump_reader *dump_file_get_reader(const struct options *options, struct dump_header *dfh);

#endif // READER_H
