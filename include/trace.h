#pragma once
#ifndef TRACE_H
#define TRACE_H

#if defined(NDEBUG)

#define __trace(...)

#else // NDEBUG

#include <stdio.h>
#define __trace(FMT, ...) \
	fprintf(stderr, "# trace %s() " FMT "\n", __func__ __VA_OPT__(,) __VA_ARGS__)

#endif // NDEBUG

#endif // TRACE_H
