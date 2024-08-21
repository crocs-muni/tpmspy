#if !defined(MSG_H)
#define MSG_H

#include <err.h>
#include <stdio.h>

#define die(...) \
	errx(EXIT_FAILURE, __VA_ARGS__)
#define croak(...) \
	err(EXIT_FAILURE, __VA_ARGS__);

#define bug(FORMAT, ...) \
	warnx("%s(): Bug: " FORMAT, __func__ __VA_OPT__(,) __VA_ARGS__)
#define BUG(FORMAT, ...) \
	errx(EXIT_FAILURE, "%s(): BUG: " FORMAT, __func__ __VA_OPT__(,) __VA_ARGS__)

#define warn_v(V, ...) \
	(warn(__VA_ARGS__), (V))
#define warnx_v(V, ...) \
	(warnx(__VA_ARGS__), (V))
#define warn_jmp(L, ...) \
	do { warn(__VA_ARGS__); goto L; } while (0)
#define warnx_jmp(L, ...) \
	do { warnx(__VA_ARGS__); goto L; } while (0)

#define info(FORMAT, ...) \
	printf("\x1b[33m" FORMAT "\x1b[0m\n" __VA_OPT__(,) __VA_ARGS__)

#endif // MSG_H
