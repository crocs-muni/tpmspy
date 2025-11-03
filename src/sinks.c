#if __SANITIZE_ADDRESS__
#	define _GNU_SOURCE
#endif

#include "sinks.h"

#include <assert.h>
#include <err.h>

#include <dlfcn.h>
#include <stdlib.h>

#include "msg.h"
#include "strings.h"
#include "trace.h"


struct sinks_context {
	size_t ref_count;
	sink_obj objs[];
};


void *dlopen2(const char *file, int mode)
{
#if __SANITIZE_ADDRESS__
	/* libasan provides its own dlopen() which does not consult RUN_PATH,
	 * breaking modules. Source: https://github.com/ROCm/TheRock/issues/1783 */

	/* First, try to get the libc's internal dlopen. */
	extern void *__libc_dlopen_mode (const char *, int) __attribute__((weak));
	if (__libc_dlopen_mode != nullptr)
		return __libc_dlopen_mode(file, mode);

        /* Otherwise we try to get versioned symbol using ASAN's loader. */
        typedef void *(*dlopen_t)(const char *, int);
        dlopen_t f;

        if ((f = (dlopen_t) dlvsym(RTLD_NEXT, "dlopen", "GLIBC_2.2.5")) != nullptr)
		return f(file, mode);

	if ((f = (dlopen_t) dlvsym(RTLD_DEFAULT, "dlopen", "GLIBC_2.2.5")) != nullptr)
		return f(file, mode);

	/* Fallback to dlopen(). */
#endif

	return dlopen(file, mode);
}

#define dlopen(F, M) \
	dlopen2(F, M)

static inline
size_t _sinks_context_size(size_t capacity)
{
	return sizeof(struct sinks_context) + sizeof(((struct sinks_context *) nullptr)->objs[0]) * capacity;
}

static inline
size_t _sinks_size(size_t capacity)
{
	return sizeof(struct sinks) + sizeof(((struct sinks *) nullptr)->syms[0]) * capacity;
}

[[nodiscard]] static inline
struct sinks *_sinks_new(size_t capacity)
{
	struct sinks *sinks = malloc(_sinks_size(capacity));

	if (sinks == nullptr)
		return nullptr;

	sinks->capacity = capacity;
	sinks->size = 0;

	return sinks;
}

static
bool _sinks_load_lib(struct sinks *sinks, size_t i, char *name)
{
	assert(i < sinks->capacity);

	bool status = false;
	char *path = nullptr;

	auto sym = &sinks->syms[i];

	if ((sym->args = strings_new()) == nullptr)
		warn_jmp(leave, "strings_new");

	if (strings_strtok(&sym->args, name, ",:") < 1)
		warn_jmp(free_args, "strings_strtok");

	sym->name = sym->args->data[0];

	if (asprintf(&path, "lib%s.so", sym->args->data[0]) == -1)
		warn_jmp(free_args, "Cannot load %s: asprintf(): ", name);

	if ((sym->dlobj = dlopen(path, RTLD_NOW | RTLD_LOCAL)) == nullptr)
		warnx_jmp(free_args, "%s", dlerror());

	if ((sym->send = (sink_send_f) dlsym(sym->dlobj, "sink_send")) == nullptr)
		warnx_jmp(free_dlobj, "%s", dlerror());

	sym->open = (sink_open_f) dlsym(sym->dlobj, "sink_open");
	sym->close = (sink_close_f) dlsym(sym->dlobj, "sink_close");

	if (sym->open != nullptr && sym->close == nullptr)
		warnx_jmp(free_dlobj, "%s: sink_open() provided without sink_close()", name);
	if (sym->open == nullptr && sym->close != nullptr)
		warnx_jmp(free_dlobj, "%s: sink_close() provided without sink_open()", name);

	status = true;
	goto leave;

free_dlobj:
	dlclose(sym->dlobj);

free_args:
	strings_free(&sym->args);

leave:
	free(path);
	return status;
}

[[nodiscard]]
struct sinks *sinks_load(size_t size, char *libs[size])
{
	struct sinks *sinks = _sinks_new(size);

	if (sinks == nullptr)
		return nullptr;

	for (size_t i = 0; i < size; i++) {
		assert(sinks->size < sinks->capacity);

		if (!_sinks_load_lib(sinks, i, libs[i])) {
			sinks_free(&sinks);
			return nullptr;
		}

		sinks->size++;
	}

	assert(sinks->size == size);
	return sinks;
}

void sinks_free(struct sinks **psinks)
{
	assert(psinks != nullptr);
	assert(*psinks != nullptr);

	for (size_t i = 0; i < (*psinks)->size; i++) {
		auto sym = &(*psinks)->syms[i];

		if (sym->dlobj != nullptr)
			dlclose(sym->dlobj);

		if (sym->args != nullptr)
			strings_free(&sym->args);
	}

	free(*psinks);
	*psinks = nullptr;
}

struct sinks_context *sinks_open(const struct sinks *sinks, struct context *ctx)
{
	assert(sinks != nullptr);

	struct sinks_context *ctxt = malloc(_sinks_context_size(sinks->size));

	if (ctxt == nullptr)
		return warn_v(nullptr, "malloc()");

	ctxt->ref_count = 1;

	for (size_t i = 0; i < sinks->size; i++) {
		auto sink = &sinks->syms[i];

		ctxt->objs[i] = nullptr;
		if (sink->open != nullptr && (ctxt->objs[i] = sink->open(sink->args, ctx)) == nullptr)
			warnx_jmp(error_open, "%s: sink_open(): Failed to open sink", sink->name);
	}

	return ctxt;

error_open:
	sinks_close(sinks, ctxt);
	return nullptr;
}

struct sinks_context *sinks_dup(struct sinks_context *ctxt)
{
	assert(ctxt != nullptr);
	assert(ctxt->ref_count >= 1);

	__trace("%zd -> %zd", ctxt->ref_count, ctxt->ref_count + 1);

	ctxt->ref_count++;
	return ctxt;
}

void sinks_close(const struct sinks *sinks, struct sinks_context *ctxt)
{
	assert(sinks != nullptr);
	assert(ctxt != nullptr);
	assert(ctxt->ref_count >= 1);

	__trace("%zd -> %zd", ctxt->ref_count, ctxt->ref_count - 1);

	if (--ctxt->ref_count > 0)
		return;

	__trace("Cleaning up");

	for (size_t i = 0; i < sinks->size; i++) {
		if (ctxt->objs[i] != nullptr && !sinks->syms[i].close(ctxt->objs[i]))
			warnx("%s: Sink did not close cleanly", sinks->syms[i].name);
	}

	free(ctxt);
}

void sinks_dispatch(const struct sinks *sinks, struct sinks_context *ctxt, const struct sink_message *msg)
{
	assert(sinks != nullptr);
	assert(ctxt != nullptr);
	assert(ctxt->ref_count >= 1);

	for (size_t i = 0; i < sinks->size; i++) {
		auto sink = &sinks->syms[i];
		assert(sink->send != nullptr);

		if (!sink->send(ctxt->objs[i], msg))
			warnx("%s: Failed to dispatch a message", sink->name);
	}
}
