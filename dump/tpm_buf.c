#include "tpm_buf.h"

#include <assert.h>
#include <string.h>

#include "msg.h"

bool tpm_bufrd(struct tpm_buf *buf, size_t data_len, unsigned char data[data_len])
{
	assert(buf != nullptr);

	if (data_len == 0) {
		bug("data_len == 0");
		return false;
	}

	if (buf->cursor + data_len > buf->size) {
		bug("Buffer overflow while reading (cursor %zu/%zu, wants %zu)",
				buf->cursor, buf->size, data_len);
		return false;
	}

	memcpy(data, &buf->buf[buf->cursor], data_len);
	buf->cursor += data_len;
	return true;
}

#define warn_trace(RV) \
	(warnx("    at %s:%d", __func__, __LINE__), (RV))

bool tpm_bufrd_tpml_pcr_selection(
		struct tpm_buf *buf, struct tpml_pcr_selection *tps)
{
	if (!tpm_bufrd_u32(buf, &tps->count))
		return warn_trace(false);

	for (typeof(tps->count) i = 0; i < tps->count; i++) {
		if (!tpm_bufrd_tpms_pcr_selection(buf, &tps->selection[i]))
			return warn_trace(false);
	}

	return true;
}

bool tpm_bufrd_tpms_pcr_selection(
		struct tpm_buf *buf, struct tpms_pcr_selection *tps)
{
	if (!tpm_bufrd_tpmi_alg_hash(buf, &tps->hash))
		return warn_trace(false);

	if (!tpm_bufrd_u8(buf, &tps->select_size))
		return warn_trace(false);

	for (typeof(tps->select_size) i = 0; i < tps->select_size; i++) {
		if (!tpm_bufrd_u8(buf, &tps->select[i]))
			return warn_trace(false);
	}

	return true;
}
