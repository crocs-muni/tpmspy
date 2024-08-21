#if !defined(DEFS_H)
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

#if __STDC_VERSION__ >= 202311L
#	define noreturn [[noreturn]]
#elif defined(__GNUC__)
#	define noreturn __attribute__((noreturn))
#else
#	define noreturn
#endif

#if __STDC_VERSION__ >= 202311L
#	define unused [[maybe_unused]]
#elif defined(__GNUC__)
#	define unused __attribute__((unused))
#else
#	define unused
#endif

#endif
