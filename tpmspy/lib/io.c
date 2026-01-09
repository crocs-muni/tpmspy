#include "io.h"

#include <assert.h>
#include <stdlib.h>

#include <unistd.h>

#include "msg.h"
#include "socket.h"

struct io *io_dup(struct io *io)
{
	assert(io != nullptr);
	assert(io->refs > 0);

	io->refs++;
	return io;
}

bool io_close(struct io *io)
{
	assert(io != nullptr);
	assert(io->refs > 0);

	if (io->refs > 1) {
		io->refs--;
		return false;
	}

	switch (io->type) {
	case IO_CLOSED:
		/* NOP */;
		break;

	case IO_STD:
	case IO_SIGFD:
		close(io->std);
		break;

	case IO_SOCKET:
		socket_close(io->socket);
		break;

	default:
		bug("io_close() on unknown IO_(%02x)", io->type);
	}

	return true;
}

void io_chain_create(struct io **head)
{
	assert(head != nullptr);
	*head = nullptr;
}

void io_chain_destroy(struct io **head)
{
	assert(head != nullptr);

	while (*head != nullptr) {
		struct io *next = (*head)->next;
		free(*head);
		*head = next;
	}
}

struct io *io_chain_append(struct io **head, struct io *node)
{
	assert(head != nullptr);
	assert(node != nullptr);

	node->next = nullptr;

	if (*head == nullptr) {
		*head = node;
		return node;
	}

	struct io *cursor = *head;
	while (cursor->next != nullptr)
		cursor = cursor->next;

	cursor->next = node;
	return node;
}
