#pragma once
#ifndef STRSCPY_H
#define STRSCPY_H

#include <stddef.h>

#include <sys/types.h>

/* This handy function is implemented in Linux Kernel. */
ssize_t strscpy(char dst[restrict], const char src[restrict], size_t size);

#endif
