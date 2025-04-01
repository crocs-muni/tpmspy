#pragma once
#ifndef TPM_BUF_H
#define TPM_BUF_H

#include <stddef.h>
#include <stdbool.h>

#include "structs/tpm_def.h"
#include "structs/tpml_pcr_selection.h"
#include "structs/tpms_pcr_selection.h"

struct tpm_buf {
	unsigned char *buf;
	size_t size;
	size_t cursor;
};

bool tpm_bufrd(struct tpm_buf *buf, size_t data_len, unsigned char data[data_len]);

static inline
bool tpm_bufend(struct tpm_buf *buf)
{
	return buf->cursor >= buf->size;
}

#define _tpm_bufrd_scalar(TYPE) \
	static inline \
	bool tpm_bufrd_ ## TYPE(struct tpm_buf *buf, TYPE *v) \
	{ return tpm_bufrd(buf, sizeof(*v), (unsigned char *) v); }

_tpm_bufrd_scalar(u8)
_tpm_bufrd_scalar(u16)
_tpm_bufrd_scalar(u32)

_tpm_bufrd_scalar(tpm2_alg_id)
_tpm_bufrd_scalar(tpmi_alg_hash)

bool tpm_bufrd_tpml_pcr_selection(
		struct tpm_buf *buf, struct tpml_pcr_selection *tps);
bool tpm_bufrd_tpms_pcr_selection(
		struct tpm_buf *buf, struct tpms_pcr_selection *tps);

#endif // TPM_BUF_H
