#pragma once
#ifndef ASSERTS_H
#define ASSERTS_H

#include <assert.h>

#include "msg.h"

#if !defined(NDEBUG)
#	define Assert(EXPR, MESSAGE) \
		do { \
			if (!(EXPR)) \
				BUG("%s: %s", #EXPR, (MESSAGE)); \
		} while (0)
#else
#	define Assert(EXPR, MESSAGE)
#endif

#endif // ASSERTS_H
