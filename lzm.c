#define _GNU_SOURCE
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fts.h>
#include <errno.h>

#include "conf.h"
#include "lzm.h"

#define LZM_NO_COMPRESSION (0x80000000UL)

long pagesize;

struct compress_args {
	struct stat *st;
	char *filename;
	char filename_out[MAXPATHLEN];
	unsigned int compress;
	unsigned int format;
	unsigned int level;
	unsigned int chunk_size;
	unsigned int console;
	unsigned int clobber;
	unsigned int recurse;
	unsigned int remove;
	unsigned int benchmark;
	unsigned int verbose;
	unsigned int test;
	unsigned int bench_tests;
};

static void
usage(void)
{
	printf("usage: lzm [options] <files...>\n");
	printf("	-0		no compression\n");
	printf("	-1		fast compression\n");
	printf("	-2 .. -6	high compression\n");
	printf("	-c		write output to stdout\n");
	printf("	-b <tests>	benchmark mode\n");
	printf("	-d		decompress file\n");
	printf("	-f		overwrite output file\n");
	printf("	-k		keep input file\n");
	printf("	-r		recurse into directories\n");
	printf("	-t		test compressed file\n");
	printf("	-v		be verbose\n");
	printf("	-h		this help message\n");
	printf("	-x <size>	chunk size for compression (KB)\n");
}

static inline unsigned int
read_data(int fd, void *buffer, unsigned int *size)
{
	unsigned int resid;
	int ret;

	resid = *size;
	while (resid > 0) {
		ret = read(fd, buffer, resid);
		if (ret < 0)
			return errno;
		if (ret == 0)
			break;
		resid -= ret;
		buffer = (char *)buffer + ret;
	}

	*size -= resid;
	return 0;
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
compress_fd(const int fd_in, const int fd_out,
    const struct compress_args * const args)
{
	struct lzm_state *state = NULL;
	unsigned char *buffer_in = NULL;
	unsigned char *buffer_out = NULL;
	unsigned char *write_buffer = NULL;
	off_t total_in = 0;
	off_t total_out = 0;
	unsigned int size_out;
	unsigned int size_in;
	unsigned int size_flag;
	unsigned int write_size;
	unsigned int header;
	int ret;

	ret = posix_memalign((void **)&buffer_in, pagesize, args->chunk_size);
	if (ret != 0) {
		ret = ENOMEM;
		fprintf(stderr, "File %s: failed to allocate %d bytes: %s\n",
		    args->filename, args->chunk_size, strerror(ret));
		goto out;
	}

	ret = posix_memalign((void **)&buffer_out, pagesize, args->chunk_size);
	if (ret != 0) {
		ret = ENOMEM;
		fprintf(stderr, "File %s: failed to allocate %d bytes: %s\n",
		    args->filename, args->chunk_size, strerror(ret));
		goto out;
	}

	ret = lzm_encode_init(&state, args->format, args->level);
	if (ret != 0) {
		fprintf(stderr, "File %s: failed to init lzm: %s\n",
		    args->filename, strerror(ret));
		goto out;
	}

	header = HEADER_VALUE;
	ret = write_data(fd_out, &header, sizeof(header));
	if (ret != 0) {
		fprintf(stderr, "File %s: failed to write data: %s\n",
		    args->filename_out, strerror(ret));
		goto out;
	}

	total_out += sizeof(header);

	ret = write_data(fd_out, &args->format, sizeof(args->format));
	if (ret != 0) {
		fprintf(stderr, "File %s: failed to write data: %s\n",
		    args->filename_out, strerror(ret));
		goto out;
	}

	total_out += sizeof(args->format);

	ret = write_data(fd_out, &args->chunk_size, sizeof(args->chunk_size));
	if (ret != 0) {
		fprintf(stderr, "File %s: failed to write data: %s\n",
		    args->filename_out, strerror(ret));
		goto out;
	}

	total_out += sizeof(args->chunk_size);

	for (;;) {

		size_in = args->chunk_size;
		ret = read_data(fd_in, buffer_in, &size_in);
		if (ret != 0) {
			fprintf(stderr, "File %s: failed to read data: %s\n",
			    args->filename, strerror(ret));
			goto out;
		}

		if (size_in == 0)
			break;

		size_out = args->chunk_size;
		size_flag = 0;
		write_buffer = buffer_out;
		ret = lzm_encode(state, buffer_in, size_in, buffer_out,
		    &size_out);
		if (ret == EOVERFLOW &&
		    args->chunk_size < LZM_NO_COMPRESSION) {
			size_out = size_in;
			size_flag = LZM_NO_COMPRESSION;
			write_buffer = buffer_in;
			ret = 0;
		}

		if (ret != 0) {
			fprintf(stderr, "File %s: failed to encode data: %s\n",
			    args->filename, strerror(ret));
			goto out;
		}

		write_size = size_out | size_flag;
		ret = write_data(fd_out, &write_size, sizeof(write_size));
		if (ret != 0) {
			fprintf(stderr, "File %s: failed to write data: %s\n",
			    args->filename_out, strerror(ret));
			goto out;
		}

		ret = write_data(fd_out, write_buffer, size_out);
		if (ret != 0) {
			fprintf(stderr, "File %s: failed to write data: %s\n",
			    args->filename_out, strerror(ret));
			goto out;
		}

		total_in += size_in;
		total_out += size_out + sizeof(size_out);
	}

	ret = 0;

 out:

	lzm_encode_finish(state);

	if (buffer_in != NULL)
		free(buffer_in);
	if (buffer_out != NULL)
		free(buffer_out);

	if (args->verbose == true && ret == 0 && fd_out != STDOUT_FILENO) {
		float perc = (float)total_out / (float)total_in * (float)100;
		printf("Compressed %s: in %ld, out %ld, %.4f%%\n",
		    args->filename_out, total_in, total_out, perc);
	}

	return ret;
}

static unsigned int
decompress_fd(const int fd_in, const int fd_out,
    struct compress_args * const args)
{
	struct lzm_state *state = NULL;
	unsigned char *buffer_in = NULL;
	unsigned char *buffer_out = NULL;
	unsigned char *write_buffer = NULL;
	off_t total_in = 0;
	off_t total_out = 0;
	unsigned int size_out;
	unsigned int size_in;
	unsigned int header;
	unsigned int bytes;
	unsigned int no_compression;
	int ret;

	bytes = sizeof(header);
	ret = read_data(fd_in, &header, &bytes);
	if (ret != 0) {
		fprintf(stderr, "File %s: failed to read data: %s\n",
		    args->filename, strerror(ret));
		goto out;
	}

	if (bytes != sizeof(header)) {
		ret = EIO;
		fprintf(stderr, "File %s: unexpected eof\n",
		    args->filename);
		goto out;
	}

	total_in += bytes;

	if (header != HEADER_VALUE) {
		ret = EINVAL;
		fprintf(stderr, "File %s: bad header value\n",
		    args->filename);
		goto out;
	}

	bytes = sizeof(args->format);
	ret = read_data(fd_in, &args->format, &bytes);
	if (ret != 0) {
		fprintf(stderr, "File %s: failed to read data: %s\n",
		    args->filename, strerror(ret));
		goto out;
	}

	if (bytes != sizeof(args->format)) {
		ret = EIO;
		fprintf(stderr, "File %s: Unexpected eof\n",
		    args->filename);
		goto out;
	}

	total_in += bytes;

	bytes = sizeof(args->chunk_size);
	ret = read_data(fd_in, &args->chunk_size, &bytes);
	if (ret != 0) {
		fprintf(stderr, "File %s: failed to read data: %s\n",
		    args->filename, strerror(ret));
		goto out;
	}

	if (bytes != sizeof(args->chunk_size)) {
		ret = EIO;
		fprintf(stderr, "File %s: Unexpected eof\n",
		    args->filename);
		goto out;
	}

	total_in += bytes;

	if (args->chunk_size == 0) {
		ret = EINVAL;
		fprintf(stderr, "File %s: Invalid chunk size\n",
		    args->filename);
		goto out;
	}

	ret = posix_memalign((void **)&buffer_in, pagesize, args->chunk_size);
	if (ret != 0) {
		ret = ENOMEM;
		fprintf(stderr,
		    "File %s: failed to allocate memory (%d bytes): %s\n",
		    args->filename, args->chunk_size, strerror(ret));
		goto out;
	}

	ret = posix_memalign((void **)&buffer_out, pagesize, args->chunk_size);
	if (ret != 0) {
		ret = ENOMEM;
		fprintf(stderr,
		    "File %s: failed to allocate memory (%d bytes): %s\n",
		    args->filename, args->chunk_size, strerror(ret));
		goto out;
	}

	ret = lzm_decode_init(&state, args->format);
	if (ret != 0) {
		fprintf(stderr, "File %s: failed to init lzm: %s\n",
		    args->filename, strerror(ret));
		goto out;
	}

	for (;;) {

		bytes = sizeof(size_in);
		ret = read_data(fd_in, &size_in, &bytes);
		if (ret != 0) {
			fprintf(stderr, "File %s: failed to read data: %s\n",
			    args->filename, strerror(ret));
			goto out;
		}

		if (bytes == 0)
			break;

		if (bytes != sizeof(size_in)) {
			ret = EIO;
			fprintf(stderr, "File %s: unexpected eof\n",
			    args->filename);
			goto out;
		}

		total_in += bytes;

		no_compression = 0;
		if (args->chunk_size < LZM_NO_COMPRESSION &&
		    (size_in & LZM_NO_COMPRESSION) != 0) {
			no_compression = 1;
			size_in &= ~LZM_NO_COMPRESSION;
		}

		if (size_in > args->chunk_size) {
			ret = EINVAL;
			fprintf(stderr, "File %s: Invalid chunk size\n",
			    args->filename);
			goto out;
		}

		bytes = size_in;
		ret = read_data(fd_in, buffer_in, &bytes);
		if (ret != 0) {
			fprintf(stderr, "File %s: failed to read data: %s\n",
			    args->filename, strerror(ret));
			goto out;
		}

		if (bytes != size_in) {
			ret = EIO;
			fprintf(stderr, "File %s: unexpected eof\n",
			    args->filename);
			goto out;
		}

		size_out = args->chunk_size;
		if (!no_compression) {
			ret = lzm_decode(state, buffer_in, size_in,
			    buffer_out, &size_out);
			if (ret != 0) {
				fprintf(stderr,
				    "File %s: failed to decode data: %s\n",
				    args->filename, strerror(ret));
				goto out;
			}
			write_buffer = buffer_out;
		} else {
			write_buffer = buffer_in;
		}

		if (args->test == false) {
			ret = write_data(fd_out, write_buffer, size_out);
			if (ret != 0) {
				fprintf(stderr, "File %s: failed to write data: %s\n",
				    args->filename_out, strerror(ret));
				goto out;
			}
		}

		total_in += size_in;
		total_out += size_out;
	}

	ret = 0;

 out:

	lzm_decode_finish(state);

	if (buffer_in != NULL)
		free(buffer_in);
	if (buffer_out != NULL)
		free(buffer_out);

	if (args->verbose == true && ret == 0 && fd_out != STDOUT_FILENO) {
		float perc = (float)total_out / (float)total_in * (float)100;
		printf("Decompressed %s: in %ld, out %ld, %.4f%%\n",
		    args->filename_out, total_in, total_out, perc);
	}

	return ret;
}

static unsigned int
output_filename(struct compress_args * const args)
{
	unsigned int len;
	unsigned int maxlen;
	int ret = 0;

	if (args->compress) {
		len = strlen(args->filename);
		maxlen = sizeof(args->filename_out) - sizeof(SUFFIX);
		if (len > maxlen)
			len = maxlen;
		strncpy(args->filename_out, args->filename, len);
		args->filename_out[len] = 0;
		strcat(args->filename_out, SUFFIX);
	} else {
		len = strlen(args->filename);
		if (len < sizeof(SUFFIX)) {
			ret = EINVAL;
			fprintf(stderr, "File %s: unknown file type\n",
			    args->filename);
			goto out;
		}

		len -= sizeof(SUFFIX) - 1;
		if (strcmp(&args->filename[len], SUFFIX) != 0) {
			ret = EINVAL;
			fprintf(stderr, "File %s: unknown file type\n",
			    args->filename);
			goto out;
		}

		if (len >= sizeof(args->filename_out))
			len = sizeof(args->filename_out) - 1;
		strncpy(args->filename_out, args->filename, len);
		args->filename_out[len] = '\0';
	}
 out:
	return ret;
}

static unsigned int
process_data(const int fd_in, const int fd_out,
    struct compress_args * const args)
{
	int ret;

	if (args->compress == true && args->test == false)
		ret = compress_fd(fd_in, fd_out, args);
	else
		ret = decompress_fd(fd_in, fd_out, args);

	return ret;
}

#define	BENCH_TIME	3000000000
#define	BENCH_TESTS	10

static inline unsigned long
gettime(void)
{
	struct timespec ts;

	if (unlikely(clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0))
		fprintf(stderr, "clock_gettime failed\n");

	return (ts.tv_sec * 1000000000 + ts.tv_nsec);
}

static inline void
synctime(void)
{
	const clock_t ts_start = clock();

	while (clock() == ts_start);
}

struct chunk {
	unsigned char *data_orig;
	unsigned char *data_comp;
	unsigned char *data_decomp;
	unsigned int size_orig;
	unsigned int size_comp;
	unsigned int size_comp_out;
	unsigned int size_decomp_out;
};

static unsigned int
benchmark_level(struct compress_args * const args, struct chunk *chunks,
    unsigned int nchunks)
{
	struct lzm_state *state = NULL;
	double rate;
	double comp_rate;
	double decomp_rate;
	double comp_perc;
	unsigned long ts_start;
	unsigned long iterations;
	unsigned long time;
	unsigned int t;
	unsigned int c;
	unsigned int ret;
	off_t comp_size;
	off_t decomp_size;
	off_t offset = 0;
	const unsigned char *d1;
	const unsigned char *d2;

	comp_rate = 0;
	ret = lzm_encode_init(&state, args->format, args->level);
	if (ret != 0) {
		fprintf(stderr, "File %s: failed to init lzm: %s\n",
		    args->filename, strerror(ret));
		goto out;
	}

	for (t = 0; t < args->bench_tests; t++) {

		iterations = 0;
		synctime();
		ts_start = gettime();

		do {
			for (c = 0; c < nchunks; c++) {
				chunks[c].size_comp_out = chunks[c].size_comp;
				ret = lzm_encode(state, chunks[c].data_orig,
				    chunks[c].size_orig, chunks[c].data_comp,
				    &chunks[c].size_comp_out);
				if (unlikely(ret != 0)) {
					fprintf(stderr,
					"File %s: failed to encode data: %s\n",
					    args->filename, strerror(ret));
					goto out;
				}
			}

			time = gettime() - ts_start;
			iterations++;

		} while (time < BENCH_TIME);

		rate = (double)(args->st->st_size * iterations * 1000) /
		    (double)time;
		if (rate > comp_rate)
			comp_rate = rate;

		if (args->verbose == true) {
			printf("%10.4f ", rate);
			fflush(stdout);
		}
	}

	if (args->verbose == true)
		printf("\n");

	lzm_encode_finish(state);

	comp_size = 0;
	for (c = 0; c < nchunks; c++)
		comp_size += chunks[c].size_comp_out;

	comp_perc = (double)(comp_size * 100) / (double)args->st->st_size;

	decomp_rate = 0;
	ret = lzm_decode_init(&state, args->format);
	if (ret != 0) {
		fprintf(stderr, "File %s: failed to init lzm: %s\n",
		    args->filename, strerror(ret));
		goto out;
	}

	for (t = 0; t < args->bench_tests; t++) {

		iterations = 0;
		synctime();
		ts_start = gettime();

		do {
			for (c = 0; c < nchunks; c++) {
				chunks[c].size_decomp_out = chunks[c].size_orig;
				ret = lzm_decode(state, chunks[c].data_comp,
				    chunks[c].size_comp_out,
				    chunks[c].data_decomp,
				    &chunks[c].size_decomp_out);
				if (unlikely(ret != 0)) {
					fprintf(stderr,
					"File %s: failed to decode data: %s\n",
					    args->filename, strerror(ret));
					goto out;
				}
			}

			time = gettime() - ts_start;
			iterations++;

		} while (time < BENCH_TIME);

		rate = (double)(args->st->st_size * iterations * 1000) /
		    (double)time;
		if (rate > decomp_rate)
			decomp_rate = rate;

		if (args->verbose == true) {
			printf("%10.4f ", rate);
			fflush(stdout);
		}
	}

	if (args->verbose == true)
		printf("\n");

	lzm_decode_finish(state);

	decomp_size = 0;
	for (c = 0; c < nchunks; c++) {
		decomp_size += chunks[c].size_decomp_out;
		if (chunks[c].size_decomp_out != chunks[c].size_orig) {
			fprintf(stderr,
			"File %s: incorrect chunk size, expect %u, got %u\n",
			    args->filename, chunks[c].size_orig,
			    chunks[c].size_decomp_out);
		}
		d1 = chunks[c].data_orig;
		d2 = chunks[c].data_decomp;
		for (t = 0; t < chunks[c].size_orig; t++) {
			if (*d1 != *d2) {
				fprintf(stderr,
				    "File %s: corruption, offset %lu,"
				    " expect 0x%x, found 0x%x\n",
				    args->filename, offset, *d1, *d2);
				goto out;
			}
			d1++;
			d2++;
			offset++;
		}
	}

	if (decomp_size != args->st->st_size) {
		fprintf(stderr,
		"File %s: incorrect decompressed size, expect %lu, got %lu\n",
		    args->filename, args->st->st_size, decomp_size);
	}

	printf("Level %d: --> %lu, %9.4f%%, %10.4f MB/s, %10.4f MB/s\n",
	    args->level, comp_size, comp_perc, comp_rate, decomp_rate);

 out:
	return ret;
}

static unsigned int
benchmark_init_chunk(const int fd_in, struct chunk * const chunk,
    const unsigned int chunk_size, struct compress_args * const args)
{
	int ret;

	chunk->size_orig = chunk_size;
	ret = posix_memalign((void **)&chunk->data_orig, pagesize,
	    chunk->size_orig);
	if (ret != 0) {
		ret = ENOMEM;
		fprintf(stderr, "File %s: failed to allocate %d bytes: %s\n",
		    args->filename, chunk->size_orig, strerror(ret));
		goto out;
	}

	ret = posix_memalign((void **)&chunk->data_decomp, pagesize,
	    chunk->size_orig);
	if (ret != 0) {
		ret = ENOMEM;
		fprintf(stderr, "File %s: failed to allocate %d bytes: %s\n",
		    args->filename, chunk->size_orig, strerror(ret));
		goto out;
	}

	chunk->size_comp = lzm_compressed_size(chunk->size_orig);
	ret = posix_memalign((void **)&chunk->data_comp, pagesize,
	    chunk->size_comp);
	if (ret != 0) {
		ret = ENOMEM;
		fprintf(stderr, "File %s: failed to allocate %d bytes: %s\n",
		    args->filename, chunk->size_comp, strerror(ret));
		goto out;
	}

	ret = read_data(fd_in, chunk->data_orig, &chunk->size_orig);
	if (ret != 0) {
		fprintf(stderr, "File %s: failed to read data: %s\n",
		    args->filename, strerror(ret));
		goto out;
	}

	if (chunk->size_orig != chunk_size) {
		ret = EINVAL;
		fprintf(stderr, "File %s: not enough data read\n",
		    args->filename);
		goto out;
	}

 out:
	return ret;
}

static unsigned int
benchmark(const int fd_in, struct compress_args * const args)
{
	struct chunk *chunks;
	cpu_set_t cpuset;
	off_t bytes_left;
	unsigned int chunk_size;
	unsigned int c;
	unsigned int nchunks;
	unsigned int ret;
	int cpu;

	nchunks = howmany(args->st->st_size, args->chunk_size);
	chunks = calloc(nchunks, sizeof(*chunks));
	if (chunks == NULL) {
		ret = errno;
		fprintf(stderr, "File %s: failed to allocate %ld bytes: %s\n",
		    args->filename, nchunks * sizeof(*chunks), strerror(ret));
		goto out;
	}

	cpu = sched_getcpu();
	if (cpu == -1) {
		ret = errno;
		fprintf(stderr, "Failed to get cpu: %s\n", strerror(ret));
		goto out;
	}

	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);
	ret = sched_setaffinity(0, sizeof(cpuset), &cpuset);
	if (ret != 0) {
		ret = errno;
		fprintf(stderr, "Failed to set cpu affinity: %s\n",
		    strerror(ret));
		goto out;
	}

	setpriority(PRIO_PROCESS, 0, -20);

	bytes_left = args->st->st_size;
	for (c = 0; c < nchunks; c++) {
		chunk_size = MIN(bytes_left, args->chunk_size);
		bytes_left -= chunk_size;
		ret = benchmark_init_chunk(fd_in, &chunks[c], chunk_size, args);
		if (ret != 0)
			goto out;
	}

	printf("File %s: size %lu bytes\n", args->filename, args->st->st_size);

	if (args->level != LZM_LEVEL_DEF)
		benchmark_level(args, chunks, nchunks);
	else {
		for (args->level = LZM_LEVEL_NONE;
		    args->level < LZM_LEVEL_COUNT;
		    args->level++) {
			benchmark_level(args, chunks, nchunks);
		}
	}

 out:
	if (chunks != NULL) {
		for (c = 0; c < nchunks; c++) {
			if (chunks[c].data_orig != NULL)
				free(chunks[c].data_orig);
			if (chunks[c].data_comp != NULL)
				free(chunks[c].data_comp);
			if (chunks[c].data_decomp != NULL)
				free(chunks[c].data_decomp);
		}
		free(chunks);
	}

	return ret;
}

static unsigned int
process_file(struct compress_args * const args)
{
	struct stat st;
	int fd_in = -1;
	int fd_out = -1;
	unsigned int remove = false;
	int err;
	int ret;

	if (args->st->st_size == 0) {
		ret = EINVAL;
		fprintf(stderr, "File %s: zero size, skipping\n",
		    args->filename);
		goto out;
	}

	fd_in = open(args->filename, O_RDONLY);
	if (fd_in < 0) {
		ret = errno;
		fprintf(stderr, "File %s: failed to open file: %s\n",
		    args->filename, strerror(ret));
		goto out;
	}

	posix_fadvise(fd_in, 0, 0, POSIX_FADV_SEQUENTIAL);

	if (args->benchmark == true) {
		ret = benchmark(fd_in, args);
		goto out;
	}

	if (args->test == false) {
		if (args->console == true) {
			fd_out = STDOUT_FILENO;
			if (isatty(fd_out)) {
				ret = EIO;
				fprintf(stderr, "Will not write to terminal\n");
				goto out;
			}

			strcpy(args->filename_out, "(stdout)");
		} else {
			ret = output_filename(args);
			if (ret != 0)
				goto out;

			ret = stat(args->filename_out, &st);
			if (ret == 0) {
				if (args->clobber == true) {
					ret = unlink(args->filename_out);
					if (ret < 0) {
						ret = errno;
						fprintf(stderr, "File %s: cannot remove: %s\n",
						    args->filename_out, strerror(ret));
						goto out;
					}
				} else {
					ret = EEXIST;
					fprintf(stderr, "File %s: not overwriting existing file\n",
					    args->filename_out);
					goto out;
				}
			}

			fd_out = open(args->filename_out,
			    O_WRONLY|O_CREAT|O_EXCL, 0600);
			if (fd_out < 0) {
				ret = errno;
				fprintf(stderr, "File %s: failed to open file: %s\n",
				    args->filename_out, strerror(ret));
				goto out;
			}

			remove = true;
		}
	}

	ret = process_data(fd_in, fd_out, args);
	if (ret != 0) {
		if (remove == true) {
			err = unlink(args->filename_out);
			if (err < 0) {
				err = errno;
				fprintf(stderr, "File %s: cannot remove: %s\n",
				    args->filename, strerror(err));
				goto out;
			}
		}
		goto out;
	}

	if (args->remove == true && args->test == false) {
		ret = stat(args->filename, &st);
		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "File %s: cannot stat: %s\n",
			    args->filename, strerror(ret));
			goto out;
		}

		/* Check source file hasn't changed */
		if (st.st_dev == args->st->st_dev &&
		    st.st_ino == args->st->st_ino) {
			ret = unlink(args->filename);
			if (ret < 0) {
				ret = errno;
				fprintf(stderr, "File %s: cannot remove: %s\n",
				    args->filename, strerror(ret));
				goto out;
			}
		}
	}

 out:

	if (fd_in != -1 && fd_in != STDIN_FILENO)
		close(fd_in);
	if (fd_out != -1 && fd_out != STDOUT_FILENO)
		close(fd_out);

	return ret;
}

static unsigned int
process_dir(struct compress_args * const args)
{
	char *path_argv[2];
	FTS *fts;
	FTSENT *entry;
	int ret = 0;
	int err;

	path_argv[0] = args->filename;
	path_argv[1] = NULL;

	fts = fts_open(path_argv, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
	if (fts == NULL) {
		ret = errno;
		fprintf(stderr, "Dir %s: could not open: %s",
		    args->filename, strerror(ret));
		return ret;
	}

	while ((entry = fts_read(fts))) {
		switch(entry->fts_info) {
		case FTS_D:
		case FTS_DP:
		case FTS_DC:
		case FTS_SL:
		case FTS_SLNONE:
		case FTS_DOT:
			continue;

		case FTS_ERR:
		case FTS_DNR:
		case FTS_NS:
			err = errno;
			fprintf(stderr, "File %s: unable to access: %s",
			    entry->fts_path, strerror(err));
			if (ret == 0)
				ret = err;
			continue;

		case FTS_F:
			args->filename = entry->fts_path;
			args->st = entry->fts_statp;
			err = process_file(args);
			if (ret == 0)
				ret = err;
			continue;
		}
	}

	fts_close(fts);
	return ret;
}

static unsigned int
process_stdin(struct compress_args * const args)
{
	int fd_in = -1;
	int fd_out = -1;
	int ret;

	fd_in = STDIN_FILENO;
	if (isatty(fd_in)) {
		ret = EIO;
		fprintf(stderr, "Will not read from terminal\n");
		goto out;
	}
	args->filename = "(stdin)";

	if (args->test == false) {
		fd_out = STDOUT_FILENO;
		if (isatty(fd_out)) {
			ret = EIO;
			fprintf(stderr, "Will not write to terminal\n");
			goto out;
		}
		strcpy(args->filename_out, "(stdout)");
	}

	ret = process_data(fd_in, fd_out, args);

 out:
	return ret;
}

static unsigned int
process_path(struct compress_args * const args)
{
	struct stat st;
	int ret;

	if (args->filename[0] == '-' && args->filename[1] == 0) {
		ret = process_stdin(args);
		goto out;
	}

	ret = stat(args->filename, &st);
	if (ret < 0) {
		ret = errno;
		fprintf(stderr, "File %s: cannot stat: %s\n",
		    args->filename, strerror(ret));
		goto out;
	}

	if (S_ISDIR(st.st_mode)) {
		if (args->recurse == false) {
			ret = EISDIR;
			fprintf(stderr, "File %s: is a directory\n",
			    args->filename);
			goto out;
		}

		ret = process_dir(args);
		goto out;
	}

	if (!S_ISREG(st.st_mode)) {
		ret = EINVAL;
		fprintf(stderr, "File %s: not a regular file\n",
		    args->filename);
		goto out;
	}

	args->st = &st;
	ret = process_file(args);

 out:
	return ret;
}

int
main(int argc, char **argv)
{
	struct compress_args args;
	int ret = 0;
	int err;
	int c;

	args.format = LZM_FORMAT_1;
	args.level = LZM_LEVEL_DEF;
	args.compress = true;
	args.console = false;
	args.clobber = false;
	args.recurse = false;
	args.remove = true;
	args.benchmark = false;
	args.verbose = false;
	args.test = false;
	args.chunk_size = CHUNK_SIZE;
	args.bench_tests = BENCH_TESTS;

	while ((c = getopt(argc, argv, "0123456b:cdfhkrtvx:")) != EOF) {
		switch (c) {
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
			args.level = c - '0';
			break;
		case 'b':
			args.benchmark = true;
			args.bench_tests = strtoul(optarg, NULL, 0);
			if (args.bench_tests == 0 || args.bench_tests > 100) {
				printf("Tests must be non-zero and max 100.\n");
				exit(1);
			}
			break;
		case 'c':
			args.console = true;
			break;
		case 'd':
			args.compress = false;
			break;
		case 'f':
			args.clobber = true;
			break;
		case 'k':
			args.remove = false;
			break;
		case 'r':
			args.recurse = true;
			break;
		case 't':
			args.test = true;
			break;
		case 'v':
			args.verbose = true;
			break;
		case 'x':
			args.chunk_size = strtoul(optarg, NULL, 0);
			if (args.chunk_size >= (1 << 22)) {
				printf("Chunk size too large.\n");
				exit(1);
			}
			args.chunk_size <<= 10;
			break;
		case 'h':
		default:
			usage();
			exit(1);
			break;
		}
	}

	pagesize = sysconf(_SC_PAGESIZE);
	if (pagesize <= 0)
		pagesize = 4096;

	if (optind == argc) {
		usage();
		exit(1);
	}

	while (optind < argc) {
		args.filename = argv[optind];
		err = process_path(&args);
		if (ret == 0)
			ret = err;
		optind++;
	}

	return ret;
}
