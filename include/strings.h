#pragma once
#ifndef STRINGS_H
#define STRINGS_H

#include <stdlib.h>

struct strings {
	size_t capacity;
	size_t size;

	const char *data[];
};

[[nodiscard]]
struct strings *strings_new();

void strings_free(struct strings **pvec);

bool strings_add(struct strings **pvec, const char *str);
ssize_t strings_strtok(struct strings **pvec, char *str, const char *delim);

const char *strings_get(const struct strings *vec, size_t index);

ssize_t strings_find_at(const struct strings *vec, const char *str, size_t start);
ssize_t strings_find(const struct strings *vec, const char *str);

#define strings_begin(pvec) \
	&(*pvec)->data[0]

#define strings_end(pvec) \
	(&(*pvec)->data[(*pvec)->size])

#endif // STRINGS_H
