#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lzm.h"
#include "lzm_int.h"
#include "conf.h"
#include "mem.h"

struct ht_entry {
	unsigned int index;
	unsigned int token;
};

struct prev_match {
	const unsigned char *lit_start;
	const unsigned char *start;
	const unsigned char *last;
	unsigned int length;
};

/*
 * Estimate worst case size of compressed data.
 */
unsigned int
lzm_compressed_size(const unsigned int size)
{
	const unsigned int csize = size + 24;

	return (csize < size) ? size : csize;
}

static inline unsigned short
hash_fast(const unsigned long seq)
{
	return ((seq * 0xAC565CAC35000000) >> (64 - HASH_ORDER_FAST));
}

static inline unsigned short
hash_high(const unsigned int seq)
{
	return (seq * 2654435761U) >> (32 - HASH_ORDER_HIGH);
}

__attribute__((aligned(64)))
unsigned char run[9] = { 0, 8, 8, 6, 8, 5, 6, 7, 8 };

static inline int
matchlen_run(const unsigned char * const start,
    const unsigned char * const last, const unsigned char * const end,
    const unsigned int bytes)
{
	const unsigned char *curr = start;
	unsigned long currval, lastval;

	if (last < (end - 7)) {
		lastval = readmem64(last);

		if (likely(curr < (end - 7))) {
			currval = readmem64(curr);
			if (lastval != currval)
				return (__builtin_ctzl(lastval ^ currval) >> 3);
			curr += bytes;
		}
		while (curr < (end - 7)) {
			currval = readmem64(curr);
			if (lastval != currval) {
				return (curr - start) +
				    (__builtin_ctzl(lastval ^ currval) >> 3);
			}
			curr += bytes;
		}
	} else {
		lastval = readmem32(last);
	}
	if (curr < (end - 3) && readmem32(curr) == (unsigned int)lastval)
		curr += 4;
	if (curr < (end - 1) && readmem16(curr) == (unsigned short)lastval)
		curr += 2;
	if (curr < end && *curr == (unsigned char)lastval)
		curr++;

	return curr - start;
}

static inline int
matchlen(const unsigned char * const start, const unsigned char * const match,
    const unsigned char * const end)
{
	const unsigned char *curr = start;
	const unsigned char *last = match;
	unsigned long currval, lastval;
	const unsigned int off = start - match;

	if (off <= 8)
		return matchlen_run(start, match, end, run[off]);

	if (likely(curr < (end - 7))) {
		lastval = readmem64(last);
		currval = readmem64(curr);
		if (likely(lastval != currval))
			return (__builtin_ctzl(lastval ^ currval) >> 3);
		last += 8;
		curr += 8;
	}
	while (curr < (end - 7)) {
		lastval = readmem64(last);
		currval = readmem64(curr);
		if (lastval != currval) {
			return (curr - start) +
			    (__builtin_ctzl(lastval ^ currval) >> 3);
		}
		last += 8;
		curr += 8;
	}
	if (curr < (end - 3)) {
		lastval = readmem32(last);
		currval = readmem32(curr);
		if (lastval != currval) {
			return (curr - start) +
			    (__builtin_ctz(lastval ^ currval) >> 3);
		}
		last += 4;
		curr += 4;
	}
	if (curr < (end - 1) && readmem16(last) == readmem16(curr)) {
		last += 2;
		curr += 2;
	}
	if (curr < end && *last == *curr)
		curr++;

	return curr - start;
}

static inline int
matchlen_rev(const unsigned char * const start, const unsigned char * const match,
    const unsigned char * const start_limit, const unsigned char * const match_limit)
{
	const unsigned char *curr = start;
	const unsigned char *last = match;
	const unsigned char *next_curr;
	const unsigned char *next_last;
	const unsigned char *end;
	unsigned long currval, lastval;
	unsigned int off;

	if (start == start_limit)
		return 0;

	if (match == match_limit)
		return 0;

	if (*(start-1) != *(match-1))
		return 0;

	end = match_limit;
	off = start - start_limit;
	if (off < (match - match_limit))
		end = match - off;

	if (last > (end + 7)) {
		next_curr = curr - 8;
		next_last = last - 8;
		currval = readmem64(next_curr);
		lastval = readmem64(next_last);
		if (currval != lastval)
			return (__builtin_clzl(currval ^ lastval) >> 3);
		curr = next_curr;
		last = next_last;
	}
	while (last > (end + 7)) {
		next_curr = curr - 8;
		next_last = last - 8;
		currval = readmem64(next_curr);
		lastval = readmem64(next_last);
		if (currval != lastval) {
			off = __builtin_clzl(currval ^ lastval) >> 3;
			return (start - curr) + off;
		}
		curr = next_curr;
		last = next_last;
	}
	if (last > (end + 3)) {
		next_curr = curr - 4;
		next_last = last - 4;
		currval = readmem32(next_curr);
		lastval = readmem32(next_last);
		if (currval != lastval) {
			off = __builtin_clz(currval ^ lastval) >> 3;
			return (start - curr) + off;
		}
		curr = next_curr;
		last = next_last;
	}
	if (last > (end + 1)) {
		next_curr = curr - 2;
		next_last = last - 2;
		currval = readmem16(next_curr);
		lastval = readmem16(next_last);
		if (readmem16(next_curr) == readmem16(next_last)) {
			curr = next_curr;
			last = next_last;
		}
	}
	if (last > end) {
		next_curr = curr - 1;
		next_last = last - 1;
		if (*next_curr == *next_last) {
			curr = next_curr;
			last = next_last;
		}
	}

	return start - curr;
}

struct offset_map {
	unsigned int bytes;
	unsigned int prefix;
};

__attribute__((aligned(64)))
const struct offset_map offmap[32] = {
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 4, 8 },
	{ 4, 8 },
	{ 4, 8 },
	{ 4, 8 },
	{ 4, 8 },
	{ 4, 8 },
	{ 4, 8 },
	{ 3, 4 },
	{ 3, 4 },
	{ 3, 4 },
	{ 3, 4 },
	{ 3, 4 },
	{ 3, 4 },
	{ 3, 4 },
	{ 2, 2 },
	{ 2, 2 },
	{ 2, 2 },
	{ 2, 2 },
	{ 2, 2 },
	{ 2, 2 },
	{ 2, 2 },
	{ 1, 1 },
	{ 1, 1 },
	{ 1, 1 },
	{ 1, 1 },
	{ 1, 1 },
	{ 1, 1 },
	{ 1, 1 }
};

static inline unsigned char *
output_offset(unsigned char * const out, const unsigned int length)
{
	const struct offset_map * const map =
	    &offmap[__builtin_clz(length | !length)];
	const unsigned int bytes = map->bytes;
	const unsigned int prefix = map->prefix;

	writemem32(out, (length << bytes) | prefix);

	return out + bytes;
}

static inline unsigned char *
output_length(unsigned char *out, const unsigned int length)
{
	if (likely(length < 252)) {
		*out++ = length;
	} else if (likely(length < (256+252))) {
		*out++ = 252;
		*out++ = length - 252;
	} else if (likely(length < (65536+253))) {
		*out++ = 253;
		writemem16(out, length - 253);
		out += 2;
	} else if (likely(length < (16777216+254))) {
		*out++ = 254;
		writemem32(out, length - 254);
		out += 3;
	} else {
		*out++ = 255;
		writemem32(out, length - 255);
		out += 4;
	}

	return out;
}

static inline unsigned char *
output_literals_op(unsigned char * const op, unsigned char *out,
    const unsigned char * const start, const unsigned int length)
{
	if (length > 0) {
		if (length < 15) {
			*op = length << 4;
			memcpy(out, start, 16);
		} else {
			*op = 15 << 4;
			out = output_length(out, length - 15);
			memcpy(out, start, length);
		}
		out += length;
	}

	return out;
}

static inline unsigned char *
output_match_op(unsigned char * const op, unsigned char *out,
    const unsigned int length)
{
	if (length < 15) {
		*op |= length;
	} else {
		*op |= 15;
		out = output_length(out, length - 15);
	}

	return out;
}

static inline unsigned char *
output_data(unsigned char *out, const unsigned char * const start,
    const unsigned int literals, const unsigned int offset,
    const unsigned int length)
{
	unsigned char * const op = out++;

	*op = 0;
	out = output_offset(out, offset);
	out = output_literals_op(op, out, start, literals);
	out = output_match_op(op, out, length);

	return out;
}

static inline unsigned char *
output_match(unsigned char * const out, const unsigned char * const start,
    const unsigned int literals, const unsigned int offset,
    const unsigned int length, const unsigned char * const out_limit)
{
	LOG("L %d\n", literals);
	LOG("M %d %d\n", length, offset);

	if ((out + literals + (1 + 5 + 5 + 4 + 8)) > out_limit)
		return NULL;

	return output_data(out, start, literals, offset, length - MIN_MATCH);
}

static inline unsigned char *
output_literals(unsigned char * const out, const unsigned char * const start,
    const unsigned int literals, const unsigned char * const out_limit)
{
	LOG("L %d\n", literals);

	if ((out + literals + (1 + 5 + 1 + 10)) > out_limit)
		return NULL;

	return output_data(out, start, literals, 0, 0);
}

static inline unsigned char *
output_match_last(struct prev_match * const prev, unsigned char *out,
    const unsigned char * const out_limit)
{
	out = output_match(out, prev->lit_start,
	    prev->start - prev->lit_start, prev->start - prev->last,
	    prev->length, out_limit);
	prev->lit_start = prev->start + prev->length;

	return out;
}

static inline unsigned char *
output_match_final(struct prev_match * const prev, unsigned char *out,
    const unsigned char * const end, const unsigned char * const out_limit)
{
	if (likely(prev->length > 0)) {
		out = output_match_last(prev, out, out_limit);
		if (out == NULL)
			return NULL;
	}

	return output_literals(out, prev->lit_start, end - prev->lit_start,
	    out_limit);
}

static inline unsigned char *
output_match_merge(struct prev_match * const prev, unsigned char *out,
    const unsigned char * const start, const unsigned char * const last,
    const unsigned int length, const unsigned char * const out_limit)
{
	if (likely(prev->length > 0)) {
		if (prev->start + prev->length <= start) {
			out = output_match_last(prev, out, out_limit);
		} else {
			if ((prev->start + MIN_MATCH) <= start) {
				prev->length = start - prev->start;
				out = output_match_last(prev, out, out_limit);
			}
		}
	}

	prev->start = start;
	prev->last = last;
	prev->length = length;

	return out;
}

static inline void
lzm_reset(const struct lzm_state * const state,
    const unsigned char * const buffer_in)
{
	struct ht_entry ht;
	unsigned int i;

	ht.index = 0;
	ht.token = readmem32(buffer_in);

	for (i = 0; i < state->hash_buckets; i++)
		state->last_ht[i] = ht;
}

static unsigned int
lzm_encode_none(
    const struct lzm_state * const state,
    const unsigned char * const buffer_in,
    const unsigned int size_in,
    unsigned char * const buffer_out,
    unsigned int * const size_out)
{
	unsigned char *curr_out;

	(void)state;

	curr_out = output_literals(buffer_out, buffer_in, size_in,
	    buffer_out + *size_out);
	if (curr_out == NULL)
		return EOVERFLOW;

	*size_out = curr_out - buffer_out;
	return 0;
}

static unsigned int
lzm_encode_fast(
    const struct lzm_state * const state,
    const unsigned char * const buffer_in,
    const unsigned int size_in,
    unsigned char * const buffer_out,
    unsigned int * const size_out)
{
	const unsigned char * const end = buffer_in + size_in;
	const unsigned char * const match_end = end - 7;
	const unsigned char * const scan_end = match_end - 7;
	const unsigned char * const out_limit = buffer_out + *size_out;
	const unsigned char *lit_start = buffer_in;
	const unsigned char *curr_in = buffer_in;
	const unsigned char *next_curr;
	const unsigned char *last;
	unsigned char *curr_out = buffer_out;
	struct ht_entry *last_htp;
	unsigned long int token;
	unsigned long int next_token;
	unsigned int last_token;
	unsigned int len;
	unsigned int off;
	unsigned int misses = (1 << MISS_ORDER) + 1;
	unsigned short hashval;
	unsigned short next_hashval;

	lzm_reset(state, buffer_in);

	token = readmem64(curr_in);
	hashval = hash_fast(token);
	next_token = readmem64(curr_in + 1);
	next_hashval = hash_fast(next_token);
	last_htp = &state->last_ht[hashval];
	last_htp->index = curr_in - buffer_in;
	last_htp->token = token;
	curr_in++;

	while (likely(curr_in < scan_end)) {
		token = next_token;
		hashval = next_hashval;
		next_curr = curr_in + (misses >> MISS_ORDER);
		next_token = readmem64(next_curr);
		next_hashval = hash_fast(next_token);
		last_htp = &state->last_ht[hashval];
		last = last_htp->index + buffer_in;
		last_token = last_htp->token;
		last_htp->index = curr_in - buffer_in;
		last_htp->token = token;

		if ((unsigned int)token != last_token ||
		    (curr_in - last) & ~MAX_OFFSET_MASK) {
			misses++;
			curr_in = next_curr;
			continue;
		}
		misses = (1 << MISS_ORDER) + 1;

		len = MIN_MATCH;
		len += matchlen(curr_in + len, last + len, match_end);
		off = matchlen_rev(curr_in, last, lit_start, buffer_in);
		curr_in -= off;
		last -= off;
		len += off;

		curr_out = output_match(curr_out, lit_start,
		    curr_in - lit_start, curr_in - last, len, out_limit);
		if (unlikely(curr_out == NULL))
			return EOVERFLOW;

		curr_in += len;
		lit_start = curr_in;

		token = readmem64(curr_in - 2);
		hashval = hash_fast(token);
		next_token = readmem64(curr_in);
		next_hashval = hash_fast(next_token);
		last_htp = &state->last_ht[hashval];
		last_htp->index = curr_in - 2 - buffer_in;
		last_htp->token = token;
	}

	curr_out = output_literals(curr_out, lit_start, end - lit_start,
	    out_limit);
	if (curr_out == NULL)
		return EOVERFLOW;

	*size_out = curr_out - buffer_out;
	return 0;
}

static inline unsigned int
lzm_offset_cost(const unsigned int length)
{
	return offmap[__builtin_clz(length | !length)].bytes;
}

static unsigned int
lzm_encode_high(
    const struct lzm_state * const state,
    const unsigned char * const buffer_in,
    const unsigned int size_in,
    unsigned char * const buffer_out,
    unsigned int * const size_out)
{
	const unsigned char * const end = buffer_in + size_in;
	const unsigned char * const match_end = end - 7;
	const unsigned char * const scan_end = match_end - 3;
	const unsigned char * const out_limit = buffer_out + *size_out;
	const unsigned char *curr_in = buffer_in;
	const unsigned char *next_curr;
	const unsigned char *curr_o;
	const unsigned char *last;
	const unsigned char *last_o;
	const unsigned char *match_last;
	const unsigned char *match_curr;
	const unsigned char *next_last;
	unsigned char *curr_out = buffer_out;
	struct ht_entry *last_htp;
	unsigned int token;
	unsigned int next_token;
	unsigned int last_token;
	unsigned int len;
	unsigned int off;
	unsigned int val;
	unsigned int match_len;
	unsigned int match_val;
	unsigned int curr_chain;
	unsigned int index;
	unsigned int misses = (1 << MISS_ORDER) + 1;
	unsigned short hashval;
	unsigned short next_hashval;

	struct prev_match prev;

	lzm_reset(state, buffer_in);

	prev.lit_start = buffer_in;
	prev.start = 0;
	prev.last = 0;
	prev.length = 0;

	token = readmem32(curr_in);
	hashval = hash_high(token);
	next_token = readmem32(curr_in + 1);
	next_hashval = hash_high(next_token);
	last_htp = &state->last_ht[hashval];
	index = curr_in - buffer_in;
	state->chains[index & state->chain_mask] = *last_htp;
	last_htp->index = index;
	last_htp->token = token;
	curr_in++;

	while (likely(curr_in < scan_end)) {
		token = next_token;
		hashval = next_hashval;
		next_curr = curr_in + (misses >> MISS_ORDER);
		next_token = readmem32(next_curr);
		next_hashval = hash_high(next_token);
		last_htp = &state->last_ht[hashval];
		last = last_htp->index + buffer_in;
		last_token = last_htp->token;
		index = curr_in - buffer_in;
		state->chains[index & state->chain_mask] = *last_htp;
		last_htp->index = index;
		last_htp->token = token;

		match_val = 0;
		match_len = 0;
		match_last = NULL;
		curr_chain = 1;

		for (;;) {
			if ((curr_in - last) & ~MAX_OFFSET_MASK)
				break;

			if ((token == last_token) && (match_len == 0 ||
			    curr_in[match_len] == last[match_len])) {

				len = MIN_MATCH;
				len += matchlen(curr_in + len, last + len,
				    match_end);
				off = matchlen_rev(curr_in, last,
				    prev.lit_start, buffer_in);
				curr_o = curr_in - off;
				last_o = last - off;
				len += off;
				val = len - lzm_offset_cost(curr_o - last_o);

				if (val > match_val) {
					match_val = val;
					match_len = len;
					match_last = last_o;
					match_curr = curr_o;
					if ((curr_o + len) >= scan_end)
						break;
				}
			}

			if (curr_chain++ == MAX_CHAIN_LENGTH)
				break;

			index = last - buffer_in;
			last_htp = &state->chains[index & state->chain_mask];
			next_last = last_htp->index + buffer_in;
			last_token = last_htp->token;

			if (next_last >= last)
				break;

			last = next_last;
		}

		if (match_len == 0) {
			misses++;
			curr_in = next_curr;
			continue;
		}
		misses = (1 << MISS_ORDER) + 1;

		curr_out = output_match_merge(&prev, curr_out, match_curr,
		    match_last, match_len, out_limit);
		if (unlikely(curr_out == NULL))
			return EOVERFLOW;

		match_curr += match_len;
		if (match_curr >= scan_end)
			break;

		curr_in = next_curr;
		while (curr_in < match_curr) {
			token = next_token;
			hashval = next_hashval;
			next_curr = curr_in + (misses >> MISS_ORDER);
			next_token = readmem32(next_curr);
			next_hashval = hash_high(next_token);
			last_htp = &state->last_ht[hashval];
			index = curr_in - buffer_in;
			state->chains[index & state->chain_mask] = *last_htp;
			last_htp->index = index;
			last_htp->token = token;
			curr_in = next_curr;
		}
	}

	curr_out = output_match_final(&prev, curr_out, end, out_limit);
	if (curr_out == NULL)
		return EOVERFLOW;

	*size_out = curr_out - buffer_out;
	return 0;
}

typedef unsigned int (*lzm_codec_func)(
    const struct lzm_state * const state,
    const unsigned char * const buffer_in,
    const unsigned int size_in,
    unsigned char * const buffer_out,
    unsigned int * const size_out);

struct lzm_config {
	lzm_codec_func	codec;
	unsigned int	hash_order;
	unsigned int	chain_order;
};

__attribute__((aligned(64)))
struct lzm_config lzm_encode_config[LZM_LEVEL_COUNT] = {
	{ lzm_encode_none, 		 0,  0 },
	{ lzm_encode_fast, HASH_ORDER_FAST,  0 },
	{ lzm_encode_high, HASH_ORDER_HIGH,  4 },
	{ lzm_encode_high, HASH_ORDER_HIGH,  8 },
	{ lzm_encode_high, HASH_ORDER_HIGH, 12 },
	{ lzm_encode_high, HASH_ORDER_HIGH, 16 },
	{ lzm_encode_high, HASH_ORDER_HIGH, 20 },
};

static int
lzm_malloc(void **addr, unsigned int size)
{
	int error;

	error = posix_memalign(addr, MEM_ALIGN, size);
	if (error != 0)
		error = ENOMEM;

	return error;
}

unsigned int
lzm_encode_init(struct lzm_state ** const state, const unsigned int format,
    const unsigned int level)
{
	struct lzm_state *statep;
	unsigned int ilevel = level;
	int error = 0;

	*state = NULL;

	/* Currently only support the one format */
	if (format != LZM_FORMAT_1)
		return EINVAL;

	if (ilevel == LZM_LEVEL_DEF)
		ilevel = LZM_LEVEL_FAST;

	if (ilevel >= LZM_LEVEL_COUNT)
		return EINVAL;

	error = lzm_malloc((void **)&statep, sizeof(*statep));
	if (error != 0)
		goto out;

	statep->level = ilevel;
	statep->format = format;
	statep->hash_order = lzm_encode_config[statep->level].hash_order;
	statep->hash_buckets = 1 << statep->hash_order;
	statep->chain_order = lzm_encode_config[statep->level].chain_order;
	statep->chain_mask = (1 << statep->chain_order) - 1;
	statep->last_ht = NULL;
	statep->chains = NULL;

	if (statep->hash_order > 0) {
		error = lzm_malloc((void **)&statep->last_ht,
		    sizeof(*statep->last_ht) << statep->hash_order);
		if (error != 0)
			goto out;
	}

	if (statep->chain_order > 0) {
		error = lzm_malloc((void **)&statep->chains,
		    sizeof(*statep->chains) << statep->chain_order);
		if (error != 0)
			goto out;
	}

	*state = statep;

 out:
	if (error != 0)
		lzm_encode_finish(statep);

	return error;
}

unsigned int
lzm_encode_finish(const struct lzm_state * const state)
{
	if (state != NULL) {
		if (state->last_ht != NULL)
			free(state->last_ht);
		if (state->chains != NULL)
			free(state->chains);
		free((void *)state);
	}

	return 0;
}

unsigned int
lzm_encode(const struct lzm_state * const state,
    const unsigned char * const buffer_in,
    const unsigned int size_in, unsigned char * const buffer_out,
    unsigned int * const size_out)
{
	int error;

	if (buffer_in == NULL || buffer_out == NULL)
		return EINVAL;

	if (size_in <= 16) {
		error = lzm_encode_none(state, buffer_in, size_in,
		    buffer_out, size_out);
	} else {
		error = lzm_encode_config[state->level].codec(state,
		    buffer_in, size_in, buffer_out, size_out);

		if (error == EOVERFLOW && state->level != LZM_LEVEL_NONE)
			error = lzm_encode_none(state, buffer_in, size_in,
			    buffer_out, size_out);
	}

	return error;
}
