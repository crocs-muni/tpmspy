#include "strings.h"

#include <assert.h>
#include <string.h>

#include "config.h"

static inline
size_t _strings_size(size_t capacity)
{
	return sizeof(struct strings) + sizeof(const char *) * capacity;
}

static
bool _strings_grow(struct strings **pvec)
{
	if ((*pvec)->size < (*pvec)->capacity)
		return true;

	ssize_t new_capacity = (*pvec)->capacity > 0
		? (*pvec)->capacity * 2
		: STRINGS_DEFAULT_CAPACITY;

	struct strings *new_vec = realloc(*pvec, _strings_size(new_capacity));

	if (new_vec == nullptr)
		return false;

	new_vec->capacity = new_capacity;
	*pvec = new_vec;

	return true;
}

[[nodiscard]]
struct strings *strings_new()
{
	struct strings *vec = malloc(_strings_size(STRINGS_DEFAULT_CAPACITY));

	if (vec == nullptr)
		return nullptr;

	vec->capacity = STRINGS_DEFAULT_CAPACITY;
	vec->size = 0;

	return vec;
}

void strings_free(struct strings **pvec)
{
	assert(pvec != nullptr);
	assert(*pvec != nullptr);

	free(*pvec);
	*pvec = nullptr;
}

bool strings_add(struct strings **pvec, const char *str)
{
	assert(pvec != nullptr);
	assert(*pvec != nullptr);

	if (!_strings_grow(pvec))
		return false;

	(*pvec)->data[(*pvec)->size] = str;
	(*pvec)->size++;

	return true;
}

ssize_t strings_strtok(struct strings **pvec, char *str, const char *delim)
{
	char *cursor = str;
	char *token;

	ssize_t i = 0;
	while (i++, (token = strsep(&cursor, delim)) != nullptr) {
		if (!strings_add(pvec, token))
			return -i;
	}

	return i;
}

const char *strings_get(const struct strings *vec, size_t index)
{
	assert(vec != nullptr);

	if (index > vec->size)
		return nullptr;

	return vec->data[index];
}

ssize_t strings_find_at(const struct strings *vec, const char *str, size_t start)
{
	assert(vec != nullptr);

	for (size_t i = start; i < vec->size; ++i) {
		if (vec->data[i] == nullptr) {
			if (str == nullptr)
				return i;
		} else if (str != nullptr && strcmp(vec->data[i], str) == 0) {
			return i;
		}
	}

	return -1ll;
}

inline
ssize_t strings_find(const struct strings *vec, const char *str)
{
	return strings_find_at(vec, str, 0);
}
