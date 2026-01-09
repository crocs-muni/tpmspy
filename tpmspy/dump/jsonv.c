#include "jsonv.h"

#include <assert.h>
#include <endian.h>
#include <string.h>
#include <unistd.h>

#include <json-c/json.h>
#include <sodium.h>

#include <socket.h>
#include <msg.h>

#include "defs.h"
#include "dump-structs.h"
#include "json_object.h"
#include "trace.h"

#define JSONEX_OBJECT_ADD(NAME, TYPE) \
	private inline \
	bool jsonex_object_add_ ## NAME(json_object *obj, const char *key, TYPE value) \
	{ \
		json_object *node = json_object_new_ ## NAME(value); \
		if (node == nullptr) \
			return false; \
		if (json_object_object_add(obj, key, node) != 0) { \
			json_object_put(node); \
			return false; \
		} \
		return true; \
	}

#define JSONEX_ARRAY_ADD(NAME, TYPE) \
	private inline \
	bool jsonex_array_add_ ## NAME(json_object *obj, TYPE value) \
	{ \
		json_object *node = json_object_new_ ## NAME(value); \
		if (node == nullptr) \
			return false; \
		if (json_object_array_add(obj, node) != 0) { \
			json_object_put(node); \
			return false; \
		} \
		return true; \
	}

JSONEX_OBJECT_ADD(int, int32_t)
JSONEX_OBJECT_ADD(int64, int64_t)
JSONEX_OBJECT_ADD(string, const char *)
JSONEX_OBJECT_ADD(boolean, bool)

JSONEX_ARRAY_ADD(int, int32_t)
JSONEX_ARRAY_ADD(string, const char *)

private
json_object *jsonex_object_new_object(json_object *node, const char *key)
{
	json_object *obj = json_object_new_object();

	if (obj == nullptr)
		return nullptr;

	if (json_object_object_add(node, key, obj) != 0) {
		json_object_put(obj);
		return nullptr;
	}

	return obj;
}

private
json_object *jsonex_object_new_array(json_object *node, const char *key)
{
	json_object *obj = json_object_new_array();

	if (obj == nullptr)
		return nullptr;

	if (json_object_object_add(node, key, obj) != 0) {
		json_object_put(obj);
		return nullptr;
	}

	return obj;
}

private
json_object *jsonex_array_new_object(json_object *node)
{
	json_object *arr = json_object_new_object();

	if (arr == nullptr)
		return nullptr;

	if (json_object_array_add(node, arr) != 0) {
		json_object_put(arr);
		return nullptr;
	}

	return arr;
}

private
json_object *jsonex_array_new_array(json_object *node)
{
	json_object *arr = json_object_new_array();

	if (arr == nullptr)
		return nullptr;

	if (json_object_array_add(node, arr) != 0) {
		json_object_put(arr);
		return nullptr;
	}

	return arr;
}

struct jsonv_ctx {
	struct resources *res;
	json_object *root;

	/* Non-owning JSON references. */
	const json_object *packets_ref;
};

private
void *_jsonv_create(struct resources *res)
{
	struct jsonv_ctx *ctx = malloc(sizeof(*ctx));
	if (ctx == nullptr)
		return nullptr;

	if ((ctx->root = json_object_new_object()) == nullptr)
		warn_jmp(err_json, "json: Cannot create root object");

	json_object *packets = json_object_new_array();
	if (packets == nullptr)
		warn_jmp(err_packets, "json: Cannot create packets array");

	if (json_object_object_add(ctx->root, "packets", packets) != 0)
		warn_jmp(err_packets, "json: Cannot add packets array to the root");

	ctx->packets_ref = packets;
	ctx->res = res;
	return ctx;

err_packets:
	json_object_put(ctx->root);

err_json:
	free(ctx);
	return nullptr;
}

private
bool _jsonv_destroy(void *_ctx)
{
	struct jsonv_ctx *ctx = _ctx;

	json_object_to_fd(STDOUT_FILENO, ctx->root, JSON_C_TO_STRING_PRETTY);
	json_object_put(ctx->root);

	free(ctx);
	return true;
}

#define EXPAND_SOCKET_TYPES(X) \
	X(SOCKET_TYPE_CLIENT) \
	X(SOCKET_TYPE_SERVER) \
	X(SOCKET_TYPE_LINK) \
	X(SOCKET_LINK_QEMU) \
	X(SOCKET_LINK_SWTPM)

private
bool _jsonv_populate_meta_ep(json_object *meta, const struct dump_packet_endpoint ep, const char *attr)
{
	json_object *node = json_object_new_object();
	if (node == nullptr)
		return warn_v(false, "json: Cannot create endpoint node");

	if (json_object_object_add(meta, attr, node) != 0)
		return warn_v(false, "json: Cannot add %s to meta node", attr);

	if (!jsonex_object_add_int(node, "fd", ep.fd))
		return warn_v(false, "json: Cannot create fd int");

	json_object *type = json_object_new_array();
	if (type == nullptr || json_object_object_add(node, "type", type) != 0) {
		json_object_put(type);
		return warn_v(false, "json: Cannot create fd type array");
	}

#define X(FLAG) \
	if (ep.type & (FLAG) && !jsonex_array_add_string(type, #FLAG)) \
		return warn_v(false, "json: Cannot add %s to fd type array", #FLAG);
	EXPAND_SOCKET_TYPES(X)
#undef X

	return true;
}

private
bool _json_populate_meta_fds(json_object *meta, const struct packet_ctrl *pkt)
{
	json_object *fds = json_object_new_array();
	if (fds == nullptr || json_object_object_add(meta, "fds", fds) != 0) {
		json_object_put(fds);
		return warn_v(false, "json: Cannot create fds array");
	}

	for (size_t i = 0; i < pkt->fds_sz; i++) {
		if (!jsonex_array_add_int(fds, pkt->fds[i]))
			return warn_v(false, "json: Cannot add a fd to fds array");
	}

	return true;
}

private
bool _jsonv_populate_meta_time(json_object *meta, const struct dump_time dt)
{
	json_object *time = json_object_new_object();
	if (time == nullptr || json_object_object_add(meta, "time", time) != 0) {
		json_object_put(time);
		return warn_v(false, "json: Cannot create object for time spec");
	}

	if (!jsonex_object_add_int64(time, "sec", dt.s))
		return warn_v(false, "json: Cannot add seconds to the time");
	if (!jsonex_object_add_int(time, "usec", dt.us))
		return warn_v(false, "json: Cannot add microseconds to the time");

	return true;
}

private
bool _jsonv_populate_meta(struct jsonv_ctx *, const struct packet_ctrl *pkt,
		json_object *meta)
{
	if (!_jsonv_populate_meta_ep(meta, pkt->src, "src"))
		return false;
	if (!_jsonv_populate_meta_ep(meta, pkt->dst, "dst"))
		return false;

	if (pkt->fds_sz && !_json_populate_meta_fds(meta, pkt))
		return false;

	if (!_jsonv_populate_meta_time(meta, pkt->time))
		return false;

	return true;
}

private
bool _jsonv_start_pkt(struct jsonv_ctx *ctx, const struct packet_ctrl *pkt,
		json_object **handle, json_object **pay)
{
	assert(handle != nullptr);
	assert(pay != nullptr);

	if ((*handle = json_object_new_object()) == nullptr)
		return false;

	json_object *meta;
	if ((meta = json_object_new_object()) == nullptr)
		warn_jmp(err_handle, "json: Cannot create meta node");
	if (json_object_object_add(*handle, "meta", meta) != 0) {
		json_object_put(meta);
		warn_jmp(err_handle, "json: Cannot add meta node to packet");
	}

	if ((*pay = json_object_new_object()) == nullptr)
		warn_jmp(err_handle, "json: Cannot create packet payload");
	if (json_object_object_add(*handle, "attr", *pay) != 0) {
		json_object_put(*pay);
		warn_jmp(err_handle, "json: Cannot add packet payload to packet");
	}

	if (!_jsonv_populate_meta(ctx, pkt, meta))
		goto err_handle;

	if (json_object_array_add((json_object *) ctx->packets_ref, *handle) != 0)
		warn_jmp(err_handle, "json: Cannot pass packet to the root");

	return true;

err_handle:
	json_object_put(*handle);
	*pay = *handle = nullptr;
	return false;
}


bool _jsonv_state_blob_sf(json_object *node, uint32_t state_flags)
{
	json_object *flags = json_object_new_array();
	if (flags == nullptr || json_object_object_add(node, "state-flags", flags) != 0) {
		json_object_put(flags);
		return warn_v(false, "json: STATEBLOB: Cannot create flag array");
	}

#define X(P, NAME) \
	if (state_flags & P ## NAME && !jsonex_array_add_string(flags, #NAME)) \
		return false;
	TPM_STATE_FLAGS(X)
#undef X

	return true;
}

bool _jsonv_state_blob_bt(json_object *node, uint32_t blob_type)
{
#define X(P, NAME) \
	if (blob_type == P ## NAME || !jsonex_object_add_string(node, "blob-type", #NAME)) \
		return false;
	TPM_BLOB_TYPES(X)
#undef X

	return true;
}

private
char *_base64(size_t bin_len, const uint8_t bin[bin_len])
{
	const int b64_variant = sodium_base64_VARIANT_ORIGINAL;
	size_t b64_len = sodium_base64_ENCODED_LEN(bin_len, b64_variant);

	char *b64 = malloc(sizeof(char) * b64_len);

	if (b64 == nullptr)
		return warn_v(nullptr, "_base64");

	if (sodium_bin2base64(b64, b64_len, bin, bin_len, b64_variant) == nullptr) {
		free(b64);
		return warnx_v(nullptr, "sodium_bin2base64()");
	}

	return b64;
}

private
json_object *_jsonv_str(size_t bsz, const uint8_t b[bsz],
		char *(*f)(size_t bsz, const uint8_t b[bsz]))
{
	char *conv = f(bsz, b);

	if (conv == nullptr)
		return nullptr;

	json_object *res = json_object_new_string(conv);
	free(conv);
	return res;
}

private inline
json_object *jsonv_base64(size_t bsz, const uint8_t b[bsz])
{
	return _jsonv_str(bsz, b, &_base64);
}

private
char *_mem2hex(size_t bin_len, const uint8_t bin[bin_len])
{
	size_t hex_len = 2 * bin_len + 1;
	char *hex = malloc(sizeof(char) * hex_len);

	if (hex == nullptr)
		return warn_v(nullptr, "_hex");

	if (sodium_bin2hex(hex, hex_len, bin, bin_len) == nullptr) {
		free(hex);
		return warnx_v(nullptr, "sodium_bin2hex()");
	}

	return hex;
}

private inline
json_object *jsonv_hex(size_t bsz, const uint8_t b[bsz])
{
	return _jsonv_str(bsz, b, &_mem2hex);
}

enum packet_type {
	pt_ctrl,
	pt_data,
};

private inline
bool _jsonv_channel(json_object *node, enum packet_type pt)
{
	const char *channel = "unknown";

	switch (pt) {
	case pt_ctrl:
		channel = "ctrl";
		break;
	case pt_data:
		channel = "data";
		break;
	default:
		BUG("Unknown channel: %d", pt);
	}

	return jsonex_object_add_string(node, "channel", channel);
}

struct tpm2_cmd_id {
	uint32_t code;
	const char *name;
};

private inline
bool _jsonv_cmd(json_object *handle, const struct tpm2_cmd_id *cmd)
{
	auto req = json_object_new_object();
	if (req == nullptr || json_object_object_add(handle, "req", req) != 0)
		return false;

	if (!jsonex_object_add_int(req, "code", cmd->code))
		return false;

	if (!jsonex_object_add_string(req, "name", cmd->name))
		return false;

	return true;
}

private inline
bool _jsonv_req_init(void *_ctx, const struct packet_ctrl *pkt, json_object **handle,
		json_object **node, struct tpm2_cmd_id cmd, enum packet_type pt)
{
	if (!_jsonv_start_pkt(_ctx, pkt, handle, node))
		return false;

	if (!_jsonv_cmd(*handle, &cmd))
		return false;

	if (!_jsonv_channel(*handle, pt))
		return false;

	return true;
}

private inline
bool _jsonv_res_init(void *_ctx, const struct packet_ctrl *pkt, json_object **handle,
		json_object **node, struct tpm2_cmd_id cmd, enum packet_type pt)
{
	if (!_jsonv_start_pkt(_ctx, pkt, handle, node))
		return false;
	if (!_jsonv_cmd(*handle, &cmd))
		return false;
	if (!_jsonv_channel(*handle, pt))
		return false;
	if (!jsonex_object_add_int(*node, "code", be32toh(*(const ptm_res *)(pkt->data))))
		return false;
	return true;
}

#define TPM2_CMD_ID(TOKEN) \
	((struct tpm2_cmd_id){ .name = #TOKEN, .code = TOKEN })

#define U [[maybe_unused]]
#define _JSONV_REQ(T, CMD, TYPE) \
	private bool _jsonv_ ## T ## _req_ ## CMD ## __impl(struct jsonv_ctx *ctx, json_object *node, const struct packet_ ## T *pkt, const TYPE *data); \
	private inline bool _jsonv_ ## T ## _req_ ## CMD(void *_ctx, const struct packet_ ## T *pkt, const TYPE *data) { \
		json_object *handle, *node; \
		if (!_jsonv_req_init(_ctx, (const struct packet_ctrl *) pkt, &handle, &node, TPM2_CMD_ID(CMD_ ## CMD), pt_ ## T)) \
			return false; \
		return _jsonv_ ## T ## _req_ ## CMD ## __impl(_ctx, node, pkt, data); \
	} \
	private bool _jsonv_ ## T ## _req_ ## CMD ## __impl(U struct jsonv_ctx *ctx, U json_object *node, U const struct packet_ ## T *pkt, U const TYPE *data)

#define _JSONV_RES(T, CMD, TYPE) \
	private bool _jsonv_ ## T ## _res_ ## CMD ## __impl(struct jsonv_ctx *ctx, json_object *node, const struct packet_ ## T *pkt, const TYPE *data); \
	private bool _jsonv_ ## T ## _res_ ## CMD(void *_ctx, const struct packet_ ## T *pkt, const TYPE *data) { \
		json_object *handle, *node; \
		if (!_jsonv_res_init(_ctx, (const struct packet_ctrl *) pkt, &handle, &node, TPM2_CMD_ID(CMD_ ## CMD), pt_ ## T)) \
			return false; \
		return _jsonv_ ## T ## _res_ ## CMD ## __impl(_ctx, node, pkt, data); \
	} \
	private bool _jsonv_ ## T ## _res_ ## CMD ## __impl(U struct jsonv_ctx *ctx, U json_object *node, U const struct packet_ ## T *pkt, U const TYPE *data)


#define JSONV_CTRL_REQ(CMD, TYPE) \
	_JSONV_REQ(ctrl, CMD, TYPE)
#define JSONV_CTRL_RES(CMD, TYPE) \
	_JSONV_RES(ctrl, CMD, TYPE)
#define JSONV_CTRL_REQ_NOP(CMD, TYPE) \
	_JSONV_REQ(ctrl, CMD, TYPE) { return true; }
#define JSONV_CTRL_RES_NOP(CMD, TYPE) \
	_JSONV_RES(ctrl, CMD, TYPE) { return true; }


JSONV_CTRL_REQ_NOP(GET_CAPABILITY, ptm_cap)

JSONV_CTRL_RES(GET_CAPABILITY, ptm_cap) {
	json_object *caps = json_object_new_array();
	if (caps == nullptr || json_object_object_add(node, "caps", caps) != 0) {
		json_object_put(caps);
		return false;
	}

	int32_t flags = be32toh(*data >> 32);

#define X(PREFIX, CAP) \
	if (flags & (PREFIX ## CAP)) { \
		if (!jsonex_array_add_string(caps, #CAP)) \
			return false; \
	}
	TPM_CAP(X)
#undef X
	return true;
}


JSONV_CTRL_REQ(INIT, ptm_init) {
	return jsonex_object_add_int(node, "flags", be32toh(data->u.req.init_flags));
}

JSONV_CTRL_RES_NOP(INIT, ptm_init)


JSONV_CTRL_REQ_NOP(SHUTDOWN, ptm_res)
JSONV_CTRL_RES_NOP(SHUTDOWN, ptm_res)


JSONV_CTRL_REQ_NOP(GET_TPMESTABLISHED, ptm_est)

JSONV_CTRL_RES(GET_TPMESTABLISHED, ptm_est) {
	return jsonex_object_add_boolean(node, "established", data->u.resp.bit);
}


JSONV_CTRL_REQ(SET_LOCALITY, ptm_loc) {
	return jsonex_object_add_int(node, "locality", be32toh(data->u.req.loc));
}

JSONV_CTRL_RES_NOP(SET_LOCALITY, ptm_loc)


JSONV_CTRL_REQ_NOP(HASH_START, ptm_res)
JSONV_CTRL_RES_NOP(HASH_START, ptm_res)


JSONV_CTRL_REQ(HASH_DATA, ptm_hdata) {
	uint32_t length = be32toh(data->u.req.length);
	if (!jsonex_object_add_int(node, "length", length))
		return false;

	json_object *base64 = jsonv_base64(length, data->u.req.data);
	if (json_object_object_add(node, "data", base64))
		return false;

	return true;
}

JSONV_CTRL_RES_NOP(HASH_DATA, ptm_hdata)


JSONV_CTRL_REQ_NOP(HASH_END, ptm_res)
JSONV_CTRL_RES_NOP(HASH_END, ptm_res)


JSONV_CTRL_REQ_NOP(CANCEL_TPM_CMD, ptm_res)
JSONV_CTRL_RES_NOP(CANCEL_TPM_CMD, ptm_res)


JSONV_CTRL_REQ_NOP(STORE_VOLATILE, ptm_res)
JSONV_CTRL_RES_NOP(STORE_VOLATILE, ptm_res)


JSONV_CTRL_REQ(RESET_TPMESTABLISHED, ptm_reset_est) {
	return jsonex_object_add_int(node, "locality", be32toh(data->u.req.loc));
}

JSONV_CTRL_RES_NOP(RESET_TPMESTABLISHED, ptm_reset_est)


JSONV_CTRL_REQ(GET_STATEBLOB, ptm_getstate) {
	if (!jsonex_object_add_int(node, "offset", be32toh(data->u.req.offset)))
		return false;
	if (!_jsonv_state_blob_sf(node, be32toh(data->u.req.state_flags)))
		return false;
	if (!_jsonv_state_blob_bt(node, be32toh(data->u.req.type)))
		return false;

	return true;
}

JSONV_CTRL_RES(GET_STATEBLOB, ptm_getstate) {
	uint32_t length = be32toh(data->u.resp.length);
	uint32_t totlength = be32toh(data->u.resp.totlength);

	if (!jsonex_object_add_int(node, "length", length))
		return false;
	if (!jsonex_object_add_int(node, "total-length", totlength))
		return false;
	if (!_jsonv_state_blob_sf(node, be32toh(data->u.resp.state_flags)))
		return false;

	json_object *blob = jsonv_base64(length, data->u.resp.data);
	if (blob == nullptr || json_object_object_add(node, "data", blob) != 0) {
		json_object_put(blob);
		return false;
	}

	return true;
}


JSONV_CTRL_REQ(SET_STATEBLOB, ptm_setstate) {
	uint32_t length = be32toh(data->u.req.length);

	if (!jsonex_object_add_int(node, "length", length))
		return false;
	if (!_jsonv_state_blob_sf(node, be32toh(data->u.req.state_flags)))
		return false;
	if (!_jsonv_state_blob_bt(node, be32toh(data->u.req.type)))
		return false;

	json_object *blob = jsonv_base64(length, data->u.req.data);
	if (blob == nullptr || json_object_object_add(node, "data", blob) != 0) {
		json_object_put(blob);
		return false;
	}

	return true;
}

JSONV_CTRL_RES_NOP(SET_STATEBLOB, ptm_setstate)


JSONV_CTRL_REQ_NOP(STOP, ptm_res)
JSONV_CTRL_RES_NOP(STOP, ptm_res)


JSONV_CTRL_REQ_NOP(GET_CONFIG, ptm_getconfig)

JSONV_CTRL_RES(GET_CONFIG, ptm_getconfig) {
	uint32_t flags = be32toh(data->u.resp.flags);

	json_object *arr = json_object_new_array();

	if (arr == nullptr || json_object_object_add(node, "flags", arr) != 0) {
		json_object_put(arr);
		return false;
	}

#define X(P, NAME) \
	if (flags & P ## NAME && !jsonex_array_add_string(arr, #NAME)) \
		return false;
	TPM_CONFIG_FLAGS(X)
#undef X

	return true;
}


JSONV_CTRL_REQ_NOP(SET_DATAFD, ptm_res)
JSONV_CTRL_RES_NOP(SET_DATAFD, ptm_res)


JSONV_CTRL_REQ(SET_BUFFERSIZE, ptm_setbuffersize) {
	return jsonex_object_add_int(node, "size", be32toh(data->u.req.buffersize));
}

JSONV_CTRL_RES(SET_BUFFERSIZE, ptm_setbuffersize) {
	if (!jsonex_object_add_int(node, "size", be32toh(data->u.resp.buffersize)))
		return false;
	if (!jsonex_object_add_int(node, "min", be32toh(data->u.resp.minsize)))
		return false;
	if (!jsonex_object_add_int(node, "max", be32toh(data->u.resp.maxsize)))
		return false;

	return true;
}


JSONV_CTRL_REQ(GET_INFO, ptm_getinfo) {
	if (!jsonex_object_add_int(node, "offset", be32toh(data->u.req.offset)))
		return false;
	if (!jsonex_object_add_int(node, "pad", be32toh(data->u.req.pad)))
		return false;

	uint64_t flags = be64toh(data->u.req.flags);

	json_object *arr = json_object_new_array();
	if (arr == nullptr || json_object_object_add(node, "flags", arr) != 0) {
		json_object_put(arr);
		return false;
	}

#define X(PREFIX, NAME) \
	if (flags & PREFIX ## NAME && !jsonex_array_add_string(arr, #NAME)) \
		return false;
	SWTPM_INFO_FLAGS(X)
#undef X
	return true;
}

JSONV_CTRL_RES(GET_INFO, ptm_getinfo) {
	uint32_t length = be32toh(data->u.resp.length);
	uint32_t totlength = be32toh(data->u.resp.totlength);

	if (!jsonex_object_add_int(node, "length", length))
		return false;
	if (!jsonex_object_add_int(node, "total-length", totlength))
		return false;

	json_object *blob = jsonv_base64(length, (const uint8_t *) data->u.resp.buffer);
	if (blob == nullptr || json_object_object_add(node, "data", blob) != 0) {
		json_object_put(blob);
		return false;
	}

	return true;
}


JSONV_CTRL_REQ(LOCK_STORAGE, ptm_lockstorage) {
	return jsonex_object_add_int(node, "retries", be32toh(data->u.req.retries));
}

JSONV_CTRL_RES_NOP(LOCK_STORAGE, ptm_lockstorage)


#undef U

private
bool _jsonv_dump_blob(json_object *node, size_t sz, const uint8_t data[sz])
{
	json_object *blob = json_object_new_array();
	if (blob == nullptr || json_object_object_add(node, "blob", blob) != 0) {
		json_object_put(blob);
		return false;
	}

	static char buf[128];
	size_t cursor = 0;
	for (size_t i = 0; i < sz; i++) {
		if (i % 16 == 0)
			buf[0] = '\0';
		if (i % 16 == 0)
			buf[cursor++] = ' ';

		cursor += snprintf(&buf[cursor], sizeof(buf) - cursor, " %02x", data[i]);

		if (i % 16 == 15 || i + 1 == sz) {
			if (!jsonex_array_add_string(blob, buf))
				return false;
			cursor = 0;
		}
	}

	return true;
}

private
bool _jsonv_ctrl_req_unknown(void *_ctx, const struct packet_ctrl *pkt)
{
	struct jsonv_ctx *ctx = _ctx;
	json_object *handle, *node;

	struct tpm2_cmd_id cmd = { .code = pkt->cmd, };
	if (!_jsonv_req_init(ctx, pkt, &handle, &node, cmd, pt_ctrl))
		return false;

	return _jsonv_dump_blob(node, pkt->data_sz, pkt->data);
}

private
bool _jsonv_ctrl_res_unknown(void *_ctx, const struct packet_ctrl *pkt)
{
	struct jsonv_ctx *ctx = _ctx;
	json_object *handle, *node;

	struct tpm2_cmd_id cmd = { .code = pkt->cmd, };
	if (!_jsonv_res_init(ctx, pkt, &handle, &node, cmd, pt_ctrl))
		return false;

	return _jsonv_dump_blob(node, pkt->data_sz, pkt->data);
}

private
bool _jsonv_ctrl_unknown(void *_ctx, const struct packet_ctrl *pkt)
{
	json_object *handle, *node;
	return _jsonv_start_pkt(_ctx, pkt, &handle, &node);
}

#define JSONV_DATA_REQ(CMD, TYPE) \
	_JSONV_REQ(data, CMD, TYPE)
#define JSONV_DATA_RES(CMD, TYPE) \
	_JSONV_RES(data, CMD, TYPE)
#define JSONV_DATA_REQ_NOP(CMD, TYPE) \
	_JSONV_REQ(data, CMD, TYPE) { return true; }
#define JSONV_DATA_RES_NOP(CMD, TYPE) \
	_JSONV_RES(data, CMD, TYPE) { return true; }

#define U [[maybe_unused]]

private
const char *tpm2_str_alg(TPM2_ALG_ID alg_id)
{
	switch (alg_id) {
#define X(PREFIX, ALG) \
	case PREFIX ## ALG: \
		return #ALG;
	TPM2_ALGS(X)
#undef X

	default:
		return "unknown";
	}
}

private
bool _tpm2_pcr_extend_unwrap(struct tpm2_pcr_extend *args, const uint8_t *buf)
{
	args->pcr = *(uint32_t *)(&buf[0]);

	size_t auth_size = be32toh(*(uint32_t *)(&buf[4]));
	args->auth = (TPMS_AUTH_COMMAND *) (&buf[8]);
	args->digests = (TPML_DIGEST_VALUES *) &buf[8 + auth_size];

	return true;
}

JSONV_DATA_REQ(PCR_Extend, uint8_t) {
	struct tpm2_pcr_extend args;
	if (!_tpm2_pcr_extend_unwrap(&args, data))
		return false;

	if (!jsonex_object_add_int(node, "pcr-index", be32toh(args.pcr)))
		return false;

	json_object *digests = json_object_new_array();
	if (digests == nullptr || json_object_object_add(node, "digests", digests) != 0) {
		json_object_put(digests);
		return false;
	}

	/* TPMT_HA structures are of variable size! */
	const uint8_t *cursor = (const uint8_t *) &args.digests->digests[0];

	for (size_t dix = 0; dix < be32toh(args.digests->count); dix++) {
		json_object *digest = json_object_new_object();
		if (digest == nullptr || json_object_array_add(digests, digest) != 0) {
			json_object_put(digest);
			return false;
		}

		json_object *alg = json_object_new_object();
		if (alg == nullptr || json_object_object_add(digest, "alg", alg) != 0) {
			json_object_put(digest);
			return false;
		}

		auto pdg = (const TPMT_HA *) cursor;
		uint16_t hash_alg_id = be16toh(pdg->hashAlg);

		if (!jsonex_object_add_int(alg, "id", hash_alg_id))
			return false;
		if (!jsonex_object_add_string(alg, "name", tpm2_str_alg(hash_alg_id)))
			return false;

		size_t digest_size = 0u;

		switch (hash_alg_id) {
#define X(PREFIX, ALG, SUFFIX) \
		case TPM2_ALG_ ## ALG: \
			digest_size = PREFIX ## ALG ## SUFFIX; \
			break;
		TPM2_HASH_ALG(X)
#undef X
		}

		cursor += sizeof(pdg->hashAlg);
		cursor += digest_size;

		if (digest_size == 0)
			continue;

		json_object *str = jsonv_hex(digest_size, pdg->digest.sha);
		if (str == nullptr || json_object_object_add(digest, "digest", str) != 0) {
			json_object_put(str);
			return false;
		}
	}

	return true;
}

private
bool _tpm2_pcr_read_unwrap(struct tpm2_pcr_read *args, const uint8_t *buf)
{
	args->count = be32toh(*(uint32_t *)(&buf[0]));

	const uint8_t *cursor = &buf[4];
	for (typeof(args->count) i = 0; i < args->count; i++) {
		auto sel = &args->selection[i];

		sel->hash = be16toh(*(uint16_t *)(cursor));
		cursor += sizeof(uint16_t);

		sel->sizeofSelect = *cursor;
		cursor++;

		for (typeof(sel->sizeofSelect) octet = 0; octet < sel->sizeofSelect; octet++) {
			sel->pcrSelect[octet] = *cursor;
			cursor++;
		}
	}

	return true;
}

JSONV_DATA_REQ(PCR_Read, uint8_t) {
	struct tpm2_pcr_read args;
	if (!_tpm2_pcr_read_unwrap(&args, data))
		return false;

	json_object *selections = jsonex_object_new_array(node, "selections");
	if (selections == nullptr)
		return false;

	for (size_t i = 0; i < args.count; i++) {
		auto selection = &args.selection[i];

		json_object *snode = jsonex_array_new_object(selections);
		if (snode == nullptr)
			return false;

		json_object *alg = jsonex_object_new_object(snode, "alg");
		if (alg == nullptr)
			return false;

		if (!jsonex_object_add_int(alg, "id", selection->hash))
			return false;
		if (!jsonex_object_add_string(alg, "name", tpm2_str_alg(selection->hash)))
			return false;

		uint32_t bits = 0;
		for (short i = 0; i < selection->sizeofSelect; i++)
			bits = (bits << 8) | selection->pcrSelect[i];

		json_object *regs = jsonex_object_new_array(snode, "pcr-index");
		if (regs == nullptr)
			return false;

		for (size_t i = 0; i < 32; i++) {
			if (bits & (1 << i) && !jsonex_array_add_int(regs, i))
				return false;
		}
	}

	return true;
}


JSONV_DATA_RES(PCR_Extend, uint8_t) {
	return true;
}

private
bool _jsonv_data_req_unknown(void *_ctx, const struct packet_data *pkt)
{
	struct jsonv_ctx *ctx = _ctx;
	json_object *handle, *node;

	char buffer[64];

	switch (pkt->cmd) {
#define X(CMD, CODE) \
	case CODE: \
		snprintf(buffer, sizeof(buffer), "TPM2_CC_%s", #CMD); \
		break;
	TPM2_CC_UNIMPLEMENTED(X)
#undef X
	default:
		snprintf(buffer, sizeof(buffer), "TPM2_CC_?");
	}

	struct tpm2_cmd_id cmd_id = {
		.name = buffer,
		.code = pkt->cmd,
	};

	if (!_jsonv_req_init(ctx, (const struct packet_ctrl *) pkt, &handle, &node, cmd_id, pt_ctrl))
		return false;

	if (!jsonex_object_add_boolean(node, "unimplemented", true))
		return false;

	return _jsonv_dump_blob(node, pkt->data_sz, pkt->data);
}

#undef U


const struct dump_visitor JSON_VISITOR = {
	.create = &_jsonv_create,
	.destroy = &_jsonv_destroy,

#define X(_P, NAME, _T) \
	.v_ctrl_ ## NAME = { \
		.req = &_jsonv_ctrl_req_ ## NAME, \
		.res = &_jsonv_ctrl_res_ ## NAME, \
	},
	TPM_CMD(X)
#undef X

	.v_data_PCR_Extend = {
		.req = &_jsonv_data_req_PCR_Extend,
		.res = &_jsonv_data_res_PCR_Extend,
	},

	.v_data_PCR_Read = {
		.req = &_jsonv_data_req_PCR_Read,
	},

	.v_data_unknown = {
		.req = &_jsonv_data_req_unknown,
	},

	.v_ctrl_unknown = {
		.req = &_jsonv_ctrl_req_unknown,
		.res = &_jsonv_ctrl_res_unknown,
		.generic = &_jsonv_ctrl_unknown,
	},
};
