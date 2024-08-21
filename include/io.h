#if !defined(IO_H)
#define IO_H

struct io {
	enum {
		IO_CLOSED,
		IO_STD,
		IO_SOCKET,
		IO_SIGFD,
	} type;

	union {
		int std;
		int sigfd;
		struct socket *socket;
	};

	struct io *next;
};

#define UNINITIALISED_IO (struct io){ .type = IO_CLOSED }

void io_chain_create(struct io **head);
void io_chain_destroy(struct io **head);
struct io *io_chain_append(struct io **head, struct io *node);


#endif // IO_H
