#pragma once
#ifndef CONNTRACK_H
#define CONNTRACK_H

#include <stdbool.h>
#include <stddef.h>

struct conntrack_entry {
	int a, b;
};

struct conntrack {
	size_t size;
	size_t capacity;
	struct conntrack_entry entries[];
};

bool conntrack_init(struct conntrack **ctp);
void conntrack_destroy(struct conntrack **ctp);

bool conntrack_add(struct conntrack **ctp, struct conntrack_entry entry);
bool conntrack_remove(struct conntrack **ctp, struct conntrack_entry *entry);
size_t conntrack_disconnect(struct conntrack **ctp, int fd);

struct conntrack_entry *conntrack_find_mut(struct conntrack **ctp,
		struct conntrack_entry *prev, int fd);

const struct conntrack_entry *conntrack_find(const struct conntrack *ct,
		const struct conntrack_entry *prev, int fd);

int conntrack_find_fd(const struct conntrack *ct,
		const struct conntrack_entry *prev, int fd);

#define conntrack_other(ENTRY, FD) \
	((ENTRY).a == (FD) ? (ENTRY).b : (ENTRY).a)

#define conntrack_entry(A, B) \
	((struct conntrack_entry){ .a = (A), .b = (B) })

#endif // CONNTRACK_H
