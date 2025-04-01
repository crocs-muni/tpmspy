#pragma once
#ifndef DEFS_H
#define DEFS_H

#if !defined(private)
#define private static
#endif

#define array_size(ARRAY) \
	((sizeof(ARRAY)) / sizeof(ARRAY[0]))

#if __STDC_VERSION__ >= 202311L
#	define fallthrough [[fallthrough]]
#elif defined(__GNUC__)
#	define fallthrough __attribute__((fallthrough))
#else
#	define fallthrough
#endif

/* NOTE: Using 'noreturn' would break __attribute__((noreturn)) in some
 * 3rd-party libraries, so we use 'no_return' instead. */
#if __STDC_VERSION__ >= 202311L
#	define no_return [[noreturn]]
#elif defined(__GNUC__)
#	define no_return __attribute__((noreturn))
#else
#	define no_return
#endif

// #if __STDC_VERSION__ >= 202311L
// #	define unused [[maybe_unused]]
// #elif defined(__GNUC__)
// #	define unused __attribute__((unused))
// #else
// #	define unused
// #endif

#if __STDC_VERSION__ >= 202311L
#	define packed [[gnu::packed]]
#elif defined(__GNUC__) || defined(__clang__)
#	define packed __attribute__((packed))
#else
#	define packed
#endif

// #if __STDC_VERSION__ < 202311L
// #	define nullptr NULL
// #	define __VA_OPT__(C) (C)
// #endif

#endif
