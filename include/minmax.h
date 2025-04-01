#pragma once
#ifndef MIN_MAX_H
#define MIN_MAX_H

/* Inspired (and copied) from the Kernel. */

#if __STDC_VERSION__ >= 202311L
	#define _MINMAX(OP, A, B) \
		({ \
			typeof(A) _a = (A); \
			typeof(B) _b = (B); \
			(void)(&_a == &_b); \
			_a OP _b ? _a : _b; \
		})

	#define min(A, B) \
		_MINMAX(<, A, B)
	#define max(A, B) \
		_MINMAX(>, A, B)
#else
	#define min(A, B) \
		((A) < (B) ? (A) : (B))
	#define max(A, B) \
		((A) < (B) ? (B) : (A))
#endif

#endif // MIN_MAX_H
