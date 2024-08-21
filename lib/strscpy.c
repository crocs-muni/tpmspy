#include "strscpy.h"

#include <assert.h>
#include <errno.h>
#include <string.h>

#include <stdio.h>

ssize_t strscpy(char dst[restrict], const char src[restrict], size_t size)
{
	assert(dst != NULL);
	assert(src != NULL);

	/* An array of size zero cannot hold even an empty string. */
	if (size == 0)
		return -(errno = E2BIG);

	char *end = stpncpy(dst, src, size);

	/* We expect at least one byte to be filled with zero bytes. */
	if (end + 1 > dst + size)
		return -(errno = E2BIG);

	return (ssize_t)(end - dst);
}
