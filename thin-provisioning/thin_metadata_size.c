/*
 * Copyright (C) 2013 Red Hat, GmbH
 * 
 * This file is released under the GPL
 *
 *
 * Calculates device-mapper thin privisioning
 * metadata device size based on pool, block size and
 * maximum expected thin provisioned devices and snapshots.
 *
 */

#include <getopt.h>
#include <libgen.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*----------------------------------------------------------------*/

static char *prg;

enum numeric_options { blocksize, poolsize, maxthins, numeric, opt_end};
struct global {
	#define UNIT_ARRAY_SZ	18
	struct {
		char *chars;
		char *strings[UNIT_ARRAY_SZ];
		unsigned long long factors[UNIT_ARRAY_SZ];
	} unit;

	struct options {
		unsigned long long n[opt_end];
		char unit;
	} options;
};
#define bytes_per_sector g->unit.factors[1]

static struct global *init(void)
{
	unsigned u;
	static struct global r;
	static char *unit_strings[] = { "bytes", "sectors",
					"kilobytes", "kibibytes", "megabytes",  "mebibytes",
					"gigabytes", "gibibytes", "terabytes",  "tebibytes",
					"petabytes", "pebibytes", "exabytes",   "ebibytes",
					"zetabytes", "zebibytes", "yottabytes", "yobibytes" };

	memset(&r, 0, sizeof(r));
	r.unit.chars = "bskKmMgGtTpPeEzZyY";
	u = 0;
	r.unit.factors[u++] = 1, r.unit.factors[u++] = 512, r.unit.factors[u++] = 1024, r.unit.factors[u++] = 1000;
	for ( ; u < UNIT_ARRAY_SZ; u += 2) {
		r.unit.factors[u] = r.unit.factors[2] * r.unit.factors[u - 2];
		r.unit.factors[u+1] = r.unit.factors[3] * r.unit.factors[u - 1];
	}

	u = UNIT_ARRAY_SZ;
	while (u--)
		r.unit.strings[u] = unit_strings[u];

	r.options.unit = 's';

	return &r;
}

static unsigned get_index(struct global *g, char unit_char)
{
	char *o = strchr(g->unit.chars, unit_char);

	return o ? o - g->unit.chars : 1;
}

static void abort_prg(const char *msg)
{
	fprintf(stderr, "%s - %s\n", prg, msg);
	exit(1);
}

static void check_opts(struct options *options)
{
	if (!options->n[blocksize] || !options->n[poolsize] || !options->n[maxthins])
		abort_prg("3 arguments required!");
	else if (options->n[blocksize] & (options->n[blocksize] - 1))
  		abort_prg("block size must be 2^^N");
	else if (options->n[poolsize] < options->n[blocksize])
  		abort_prg("poolsize must be much larger than blocksize");
	else if (!options->n[maxthins])
		abort_prg("maximum number of thin provisioned devices must be > 0");
}

static unsigned long long to_bytes(struct global *g, char *sz, int div)
{
	unsigned len = strlen(sz);
	char uc = 's', *us = strchr(g->unit.chars, sz[len-1]);

	if (us)
		uc = sz[len-1], sz[len-1] = 0;

	return g->unit.factors[get_index(g, uc)] * atoll(sz) / (div ? bytes_per_sector : 1);
}

static void printf_aligned(struct global *g, char *a, char *b, char *c, int units)
{
	char buf[80];

	strcpy(buf, b);
	if (units)
		strcat(buf, "["), strcat(buf, g->unit.chars), strcat(buf, "]");

	printf("\t%-4s%-45s%s\n", a, buf, c);
}

static void help(struct global *g)
{
	printf ("Thin Provisioning Metadata Device Size Calculator.\nUsage: %s [opts]\n", prg);
	printf_aligned(g, "-b", "--block-size BLOCKSIZE", "Block size of thin provisioned devices.", 1);
	printf_aligned(g, "-s", "--pool-size SIZE", "Size of pool device.", 1);
	printf_aligned(g, "-m", "--max-thins #MAXTHINS", "Maximum sum of all thin devices and snapshots.", 0);
	printf_aligned(g, "-u", "--unit ", "Output unit specifier.", 1);
	printf_aligned(g, "-n", "--numeric-only", "Output numeric value only.", 0);
	printf_aligned(g, "-h", "--help", "This help.", 0);
	exit(0);
}

static struct global *parse_command_line(struct global *g, int argc, char **argv)
{
	int c;
	static struct option long_options[] = {
		{"block-size",	required_argument, 0,  'b' },
		{"pool-size",	required_argument, 0,  's' },
		{"max-thins",	required_argument, 0,  'm' },
		{"unit",	required_argument, 0,  'u' },
		{"numeric-only",required_argument, 0,  'n' },
		{"help",	no_argument,       0,  'h' },
		{NULL,		0,		   0,  0 }
	};

	while ((c = getopt_long(argc, argv, "b:s:m:u:nh", long_options, NULL)) != -1) {
		if (c == 'b')
      			g->options.n[blocksize] = to_bytes(g, optarg, 1);
		else if (c == 's')
      			g->options.n[poolsize] = to_bytes(g, optarg, 1);
		else if (c == 'm')
      			g->options.n[maxthins] = to_bytes(g, optarg, 0);
		else if (c == 'u') {
			if (*(optarg + 1))
				abort_prg("only one unit specifier allowed!");
			else if (!strchr(g->unit.chars, *optarg))
				abort_prg("output unit specifier invalid!");

      			g->options.unit = *optarg;
		} else if (c == 'n')
			g->options.n[numeric] = 1;
		else if (c == 'h')
			help(g);
		else
			abort_prg("Invalid option!");
	}

	check_opts(&g->options);

	return g;
}

static const unsigned mappings_per_block(void)
{
	const struct {
		const unsigned node;
		const unsigned node_header;
		const unsigned entry;
	} btree_size = { 4096, 64, 16 };

	return (btree_size.node - btree_size.node_header) / btree_size.entry;
}

static void printf_precision(double r, int full, char *unit_str)
{
	double rtrunc = truncl(r);

	/* FIXME: correct output */
	if (full)
		printf("%s - estimated metadata area size is ", prg);

	if (r == rtrunc)
		printf("%llu", (unsigned long long) r);
	else
		printf(r - truncl(r) < 1E-3 ? "%0.3e" : "%0.3f", r);

	if (full)
		printf(" %s", unit_str);

	putchar('\n');
}

static void estimated_result(struct global *g)
{
	unsigned idx = get_index(g, g->options.unit);
	double r;

	/* double-fold # of nodes, because they aren't fully populated in average */
	r = (1.0 + (2 * g->options.n[poolsize] / g->options.n[blocksize] / mappings_per_block() + g->options.n[maxthins])) * 8 * bytes_per_sector; /* in bytes! */
	r /= g->unit.factors[idx]; /* in requested unit */

	printf_precision(r, !g->options.n[numeric], g->unit.strings[idx]);
}

int main(int argc, char **argv)
{
	prg = basename(*argv);
	estimated_result(parse_command_line(init(), argc, argv));
	return 0;
}