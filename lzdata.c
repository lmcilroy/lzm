#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include "conf.h"
#include "mem.h"

#define FILE_SIZE	(100 << 20)
#define MIN_LIT_LEN	4
#define MAX_LIT_LEN	CHUNK_SIZE
#define MIN_MATCH_LEN	4
#define MAX_MATCH_LEN	CHUNK_SIZE
#define MIN_OFFSET	1
#define MAX_OFFSET	CHUNK_SIZE
#define MATCH_PROB	0.67
#define LEN_SCALE	2.0
#define MIN_MATCH	4

#ifdef DEBUG
#define LOG(fmt, ...)	do { fprintf(stderr, fmt, __VA_ARGS__); } while (0)
#else
#define LOG(fmt, ...)
#endif

struct lzdata_args {
	char		*filename;
	off_t		file_size;
	unsigned int	chunk_size;
	unsigned int	min_lit_len;
	unsigned int	max_lit_len;
	unsigned int	min_match_len;
	unsigned int	max_match_len;
	unsigned int	min_offset;
	unsigned int	max_offset;
	unsigned int	verbose;
	double		match_prob;
	double		lit_len_scale;
	double		match_len_scale;
	unsigned long	random_seed;
	unsigned long	literals;
	unsigned long	matches;
	unsigned long	literal_bytes;
	unsigned long	match_bytes;
};

enum opttype { FILESIZE, CHUNKSIZE, MINLITLEN, MAXLITLEN, MINMATCHLEN,
    MAXMATCHLEN, MINOFFSET, MAXOFFSET, MATCHPROB, LITLENSCALE, MATCHLENSCALE,
    RANDOMSEED, VERBOSE, HELP };

static struct option long_opts[] = {
    { "filesize",	required_argument,	NULL,	FILESIZE	},
    { "chunksize",	required_argument,	NULL,	CHUNKSIZE	},
    { "minlitlen",	required_argument,	NULL,	MINLITLEN	},
    { "maxlitlen",	required_argument,	NULL,	MAXLITLEN	},
    { "minmatchlen",	required_argument,	NULL,	MINMATCHLEN	},
    { "maxmatchlen",	required_argument,	NULL,	MAXMATCHLEN	},
    { "minoffset",	required_argument,	NULL,	MINOFFSET	},
    { "maxoffset",	required_argument,	NULL,	MAXOFFSET	},
    { "matchprob",	required_argument,	NULL,	MATCHPROB	},
    { "litlenscale",	required_argument,	NULL,	LITLENSCALE	},
    { "matchlenscale",	required_argument,	NULL,	MATCHLENSCALE	},
    { "randomseed",	required_argument,	NULL,	RANDOMSEED	},
    { "verbose",	no_argument,		NULL,	VERBOSE		},
    { "help",		no_argument,		NULL,	HELP		},
    { NULL,		no_argument,		NULL,	0		},
};

static void
usage(void)
{
	printf("usage: lzdata [options] <files...>\n");
	printf("	--filesize <size>	file size (MB)\n");
	printf("	--chunksize <size>	chunk size (KB)\n");
	printf("	--minlitlen <len>	minimum literal length\n");
	printf("	--maxlitlen <len>	minimum literal length\n");
	printf("	--minmatchlen <len>	minimum match length\n");
	printf("	--maxmatchlen <len>	minimum match length\n");
	printf("	--minoffset <len>	minimum offset length\n");
	printf("	--maxoffset <len>	minimum offset length\n");
	printf("	--matchprob <prob>	probability of match [0..1]\n");
	printf("	--litlenscale <val>	literal length scale (> 0)\n");
	printf("	--matchlenscale <val>	match length scale (> 0)\n");
	printf("	--randomseed <val>	random number seed\n");
	printf("	--verbose		report details used\n");
	printf("	--help			this help\n");
}

static inline double
rngd(void)
{
	return drand48();
}

static inline unsigned long
rngl(void)
{
	return mrand48();
}

static unsigned int
gen_literal(unsigned char * const buffer, unsigned int pos,
    const unsigned int size, struct lzdata_args * const args)
{
	unsigned int length;
	unsigned int end;

	length = args->lit_len_scale / (1 - rngd()) -
	    args->lit_len_scale + args->min_lit_len;

	if (length > args->max_lit_len)
		length = args->max_lit_len;

	if (pos + length > size)
		length = size - pos;

	LOG("L %d %d\n", pos, length);

	args->literals++;
	args->literal_bytes += length;

	end = pos + length;

	if (end > 3) {
		while (pos < end - 3) {
			writemem32(&buffer[pos], rngl());
			pos += 4;
		}
	}

	while (pos < end)
		buffer[pos++] = rngl();

	return pos;
}

static unsigned int
gen_match(unsigned char * const buffer, unsigned int pos,
    const unsigned int size, struct lzdata_args * const args)
{
	unsigned int length;
	unsigned int offset;
	unsigned int maxoff;
	unsigned int end;

	length = args->match_len_scale / (1 - rngd()) -
	    args->match_len_scale + args->min_match_len;

	if (length > args->max_match_len)
		length = args->max_match_len;

	if (pos + length > size)
		length = size - pos;

	maxoff = args->max_offset;
	if (maxoff > pos)
		maxoff = pos;

	offset = rngl() % (maxoff - args->min_offset + 1) +
	    args->min_offset;

	LOG("M %d %d %d\n", pos, length, offset);

	args->matches++;
	args->match_bytes += length;

	end = pos + length;

	if (end > 3) {
		while (pos < end - 3) {
			memcpy(&buffer[pos], &buffer[offset], 4);
			pos += 4;
			offset += 4;
		}
	}

	while (pos < end)
		buffer[pos++] = buffer[offset++]; 

	return pos;
}

static unsigned int
generate_data(struct lzdata_args * const args,
    unsigned char * const buffer, const unsigned int size)
{
	unsigned int pos = 0;
	int ret = 0;

	while (pos < args->min_offset)
		pos = gen_literal(buffer, pos, size, args);

	while (pos < size - (args->min_match_len - 1)) {
		if (rngd() < args->match_prob)
			pos = gen_match(buffer, pos, size, args);
		else
			pos = gen_literal(buffer, pos, size, args);
	}

	while (pos < size)
		pos = gen_literal(buffer, pos, size, args);

	return ret;
}

static inline unsigned int
write_data(int fd, const void *buffer, const size_t size)
{
	unsigned int resid;
	int ret;

	resid = size;
	while (resid > 0) {
		ret = write(fd, buffer, resid);
		if (ret < 0)
			return errno;
		resid -= ret;
		buffer = (char *)buffer + ret;
	}

	return 0;
}

static unsigned int
process_file(struct lzdata_args * const args)
{
	unsigned char *buffer = NULL;
	off_t written;
	unsigned int size;
	int fd = -1;
	int ret;

	ret = posix_memalign((void **)&buffer, getpagesize(),
	    args->chunk_size);
	if (ret != 0) {
		ret = ENOMEM;
		fprintf(stderr, "File %s: failed to allocate %d bytes: %s\n",
		    args->filename, args->chunk_size, strerror(ret));
		goto out;
	}

	fd = open(args->filename, O_WRONLY|O_CREAT|O_EXCL, 0600);
	if (fd < 0) {
		ret = errno;
		fprintf(stderr, "File %s: failed to open file: %s\n",
		    args->filename, strerror(ret));
		goto out;
	}

	args->literals = 0;
	args->literal_bytes = 0;
	args->matches = 0;
	args->match_bytes = 0;

	for (written = 0; written < args->file_size; written += size) {

		size = args->chunk_size;
		if (written + size > args->file_size)
			size = args->file_size - written;

		generate_data(args, buffer, size);

		ret = write_data(fd, buffer, size);
		if (ret != 0) {
			fprintf(stderr, "File %s: failed to write data: %s\n",
			    args->filename, strerror(ret));
			goto out;
		}
	}

	if (args->verbose == true) {
		printf("literals %ld/%ld, matches %ld/%ld, dupe data %.4f%%\n",
		    args->literals, args->literal_bytes,
		    args->matches, args->match_bytes,
		    (float)args->match_bytes * 100 /
		    (float)(args->literal_bytes + args->match_bytes));
	}

 out:

	if (buffer != NULL)
		free(buffer);

	if (fd != -1)
		close(fd);

	return ret;
}

int
main(int argc, char **argv)
{
	struct lzdata_args args;
	int ret = 0;
	int err;
	int c;

	args.file_size = FILE_SIZE;
	args.chunk_size = CHUNK_SIZE;
	args.min_lit_len = 0;
	args.max_lit_len = 0;
	args.min_match_len = 0;
	args.max_match_len = 0;
	args.min_offset = 0;
	args.max_offset = 0;
	args.verbose = false;
	args.match_prob = MATCH_PROB;
	args.lit_len_scale = LEN_SCALE;
	args.match_len_scale = LEN_SCALE;
	args.random_seed = 0;

	while ((c = getopt_long(argc, argv, "hv", long_opts, NULL)) != EOF) {
		switch (c) {
		case FILESIZE:
			args.file_size = strtoul(optarg, NULL, 0);
			if (args.chunk_size == 0) {
				fprintf(stderr, "Error: file size is zero");
				exit(1);
			}
			args.file_size <<= 20;
			break;
		case CHUNKSIZE:
			args.chunk_size = strtoul(optarg, NULL, 0);
			if (args.chunk_size == 0) {
				fprintf(stderr, "Error: chunk size is zero");
				exit(1);
			}
			args.chunk_size <<= 10;
			break;
		case MINLITLEN:
			args.min_lit_len = strtoul(optarg, NULL, 0);
			if (args.min_lit_len == 0) {
				fprintf(stderr, "Error: bad min lit len");
				exit(1);
			}
			break;
		case MAXLITLEN:
			args.max_lit_len = strtoul(optarg, NULL, 0);
			if (args.max_lit_len == 0) {
				fprintf(stderr, "Error: bad max lit len");
				exit(1);
			}
			break;
		case MINMATCHLEN:
			args.min_match_len = strtoul(optarg, NULL, 0);
			if (args.min_match_len == 0) {
				fprintf(stderr, "Error: bad min match len");
				exit(1);
			}
			break;
		case MAXMATCHLEN:
			args.max_match_len = strtoul(optarg, NULL, 0);
			if (args.max_match_len == 0) {
				fprintf(stderr, "Error: bad max match len");
				exit(1);
			}
			break;
		case MINOFFSET:
			args.min_offset = strtoul(optarg, NULL, 0);
			if (args.min_offset == 0) {
				fprintf(stderr, "Error: bad min offset");
				exit(1);
			}
			break;
		case MAXOFFSET:
			args.max_offset = strtoul(optarg, NULL, 0);
			if (args.max_offset == 0) {
				fprintf(stderr, "Error: bad max offset");
				exit(1);
			}
			break;
		case MATCHPROB:
			args.match_prob = strtod(optarg, NULL);
			if (args.match_prob < 0.0 || args.match_prob > 1.0) {
				fprintf(stderr, "Error: bad match prob");
				exit(1);
			}
			break;
		case LITLENSCALE:
			args.lit_len_scale = strtod(optarg, NULL);
			if (args.lit_len_scale < 0.0) {
				fprintf(stderr, "Error: bad lit len scale");
				exit(1);
			}
			break;
		case MATCHLENSCALE:
			args.match_len_scale = strtod(optarg, NULL);
			if (args.match_len_scale < 0.0) {
				fprintf(stderr, "Error: bad match len scale");
				exit(1);
			}
			break;
		case RANDOMSEED:
			args.random_seed = strtoul(optarg, NULL, 0);
			break;
		case 'v':
		case VERBOSE:
			args.verbose = true;
			break;
		case 'h':
		case HELP:
		default:
			usage();
			exit(1);
			break;
		}
	}

	if (args.min_lit_len == 0)
		args.min_lit_len = 1;
	if (args.max_lit_len == 0)
		args.max_lit_len = args.chunk_size;
	if (args.min_match_len == 0)
		args.min_match_len = MIN_MATCH;
	if (args.max_match_len == 0)
		args.max_match_len = args.chunk_size;
	if (args.min_offset == 0)
		args.min_offset = 1;
	if (args.max_offset == 0)
		args.max_offset = args.chunk_size;

	if (args.min_lit_len > args.max_lit_len) {
		fprintf(stderr, "Error: min lit length > max lit length\n");
		exit(1);
	}

	if (args.min_match_len > args.max_match_len) {
		fprintf(stderr, "Error: min match length > max match length\n");
		exit(1);
	}

	if (args.min_offset > args.max_offset) {
		fprintf(stderr, "Error: min offset > max offset\n");
		exit(1);
	}

	if (optind == argc) {
		usage();
		exit(1);
	}

	if (args.verbose == true) {
		printf("File size: %lu\n", args.file_size);
		printf("Chunk size: %u\n", args.chunk_size);
		printf("Min lit len: %u\n", args.min_lit_len);
		printf("Max lit len: %u\n", args.max_lit_len);
		printf("Min match len: %u\n", args.min_match_len);
		printf("Max match len: %u\n", args.max_match_len);
		printf("Min offset: %u\n", args.min_offset);
		printf("Max offset: %u\n", args.max_offset);
		printf("Match probability: %.4f\n", args.match_prob);
		printf("Lit len scale: %.4f\n", args.lit_len_scale);
		printf("Match len scale: %.4f\n", args.match_len_scale);
		printf("Random seed: %lu\n", args.random_seed);
	}

	srand48(args.random_seed);

	while (optind < argc) {
		args.filename = argv[optind];
		err = process_file(&args);
		if (ret == 0)
			ret = err;
		optind++;
	}

	return ret;
}
