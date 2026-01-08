#include "reader.h"

#include <errno.h>
#include <stdlib.h>

#include <err.h>
#include <endian.h>
#include <unistd.h>

private
void *_rd1_0_create(struct resources *res)
{
	return res;
}

private
bool _rd1_0_destroy(void *)
{
	return true;
}

private
bool _rd_pkt_reserve(struct dump_packet **pkt, size_t *pktsz, size_t new_pktsize)
{
	if (*pkt != nullptr && *pktsz >= new_pktsize)
		return true;

	struct dump_packet *new_pkt;
	if ((new_pkt = realloc(*pkt, new_pktsize)) == nullptr) {
		warn("malloc()");
		return false;
	}

	*pkt = new_pkt;
	*pktsz = new_pktsize;
	return true;
}

private
ssize_t _rd_read_all(int fd, char *buffer, size_t size)
{
	size_t received = 0u;
	ssize_t rd = 0;

	while (received < size && (rd = read(fd, &buffer[received], size - received)) > 0)
		received += rd;

	return rd;
}

private
enum dump_reader_status _rd1_0_read(void *, int fd, struct dump_packet **pkt, size_t *pktsz)
{
	if (!_rd_pkt_reserve(pkt, pktsz, sizeof(**pkt)))
		return DR_STATUS_ERRNO;

	ssize_t rd;
	if ((rd = _rd_read_all(fd, (char *) *pkt, sizeof(**pkt))) <= 0)
		return rd == 0 ? DR_STATUS_EOF : DR_STATUS_ERRNO;

	dump_packet_marshall_inplace(*pkt);

	size_t payload_size = sizeof(int) * (*pkt)->dp_fds + (*pkt)->dp_data;
	if (!_rd_pkt_reserve(pkt, pktsz, sizeof(**pkt) + payload_size))
		return DR_STATUS_ERRNO;

	if ((rd = _rd_read_all(fd, (*pkt)->bytes, payload_size)) <= 0) {
		if (rd == 0)
			errno = EINVAL;

		return DR_STATUS_ERRNO;
	}

	int *pfds = DUMP_PFDS(*pkt);
	for (size_t i = 0; i < (*pkt)->dp_fds; i++)
		pfds[i] = be32toh(pfds[i]);

	return DR_STATUS_OK;
}

private const
struct dump_reader READER_1_0 = {
	.create = &_rd1_0_create,
	.destroy = &_rd1_0_destroy,
	.read = &_rd1_0_read,
};

const struct dump_reader *dump_file_get_reader(const struct options *, struct dump_header *dfh)
{
	switch ((uint16_t)(dfh->version[0]) << 8 | dfh->version[1]) {
	case 0x01'00:
		return &READER_1_0;

	default:
		return nullptr;
	}
}
