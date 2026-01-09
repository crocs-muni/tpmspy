#include "options.h"

#include <stdio.h>
#include <stdlib.h>

#include <getopt.h>

#include <defs.h>

private const struct option LONG_OPTS[] = {
	{ "json", false, nullptr, 'j' },

	{},
};

private const char SHORT_OPTS[] = "j";

private
void usage(FILE *stream)
{
	fprintf(stream, "usage: dump [--json] FILENAME\n");
}

void options_process(struct options *options, int argc, char *argv[])
{
	int option;
	while ((option = getopt_long(argc, argv, SHORT_OPTS, LONG_OPTS, nullptr)) != -1) {
		switch (option) {
		case 'j':
			options->json = true;
			break;

		default:
			exit(EXIT_FAILURE);
		}
	}

	if (argc - optind != 1) {
		usage(stderr);
		exit(EXIT_FAILURE);
	}

	options->path = argv[optind];
}
