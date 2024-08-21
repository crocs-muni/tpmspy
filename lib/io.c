#include "io.h"

#include <assert.h>
#include <stdlib.h>

void io_chain_create(struct io **head)
{
	assert(head != NULL);
	*head = NULL;
}

void io_chain_destroy(struct io **head)
{
	assert(head != NULL);

	while (*head != NULL) {
		struct io *next = (*head)->next;
		free(*head);
		*head = next;
	}
}

struct io *io_chain_append(struct io **head, struct io *node)
{
	assert(head != NULL);
	assert(node != NULL);

	node->next = NULL;

	if (*head == NULL) {
		*head = node;
		return node;
	}

	struct io *cursor = *head;
	while (cursor->next != NULL)
		cursor = cursor->next;

	cursor->next = node;
	return node;
}
