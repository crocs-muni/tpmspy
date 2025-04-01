#include "io.h"

#include <assert.h>
#include <stdlib.h>

#include "defs.h"

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
