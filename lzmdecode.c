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

__attribute__((aligned(64)))
const unsigned int mask[5] = { 0, 0xFF, 0xFFFF, 0xFFFFFF, 0xFFFFFFFF };

static inline const unsigned char *
decode_offset(const unsigned char * const in, unsigned int * const length)
{
	const unsigned int len = readmem32(in);
	const unsigned int bytes = __builtin_ctz(len) + 1;

	*length = (len & mask[bytes]) >> bytes;

	return in + bytes;
}

static inline const unsigned char *
decode_length(const unsigned char *in, unsigned int * const length)
{
	unsigned int len;

	len = *in++;
	if (likely(len < 252)) {
		;
	} else if (likely(len == 252)) {
		len += *in;
		in += 1;
	} else if (likely(len == 253)) {
		len += readmem16(in);
		in += 2;
	} else if (likely(len == 254)) {
		len += readmem32(in) & 0xFFFFFF;
		in += 3;
	} else {
		len += readmem32(in);
		in += 4;
	}

	*length = len;
	return in;
}

unsigned int
lzm_decode_init(struct lzm_state ** const state, const unsigned int format)
{
	*state = NULL;
	(void)format;
	return 0;
}

unsigned int
lzm_decode_finish(const struct lzm_state * const state)
{
	(void)state;
	return 0;
}

unsigned int
lzm_decode(
    const struct lzm_state * const state,
    const unsigned char * const buffer_in,
    const unsigned int size_in,
    unsigned char * const buffer_out,
    unsigned int * const size_out)
{
	const unsigned char * const end = buffer_in + size_in;
	const unsigned char * const match_end = end - 5;
	const unsigned char *curr_in = (const unsigned char *) buffer_in;
	unsigned char *curr_out = (unsigned char *) buffer_out;
	const unsigned char * const out_limit = buffer_out + *size_out;
	const unsigned char *out_limit_fast_path;
	unsigned char *match;
	unsigned char *mend;
	unsigned long int c;
	unsigned int llen;
	unsigned int mlen;
	unsigned int off = 1;
	unsigned char op;

	(void)state;

	if (buffer_in == NULL || buffer_out == NULL)
		return EINVAL;

	out_limit_fast_path = (*size_out < (14 + 14 + MIN_MATCH)) ? NULL :
		out_limit - (14 + 14 + MIN_MATCH);

	while (likely(curr_in <= match_end)) {
		op = *curr_in++;
		llen = op >> 4;
		mlen = (op & 15) + MIN_MATCH;

		curr_in = decode_offset(curr_in, &off);

		if (likely(llen < 15 && (curr_in + 16) <= end &&
		    curr_out <= out_limit_fast_path)) {
			LOG("L %d\n", llen);
			memcpy(curr_out, curr_in, 16);
			curr_out += llen;
			curr_in += llen;
			if (unlikely(off > (curr_out - buffer_out)))
				return EIO;
			if (likely(mlen < (15 + MIN_MATCH) &&
			    likely(((off >= mlen) | (off >= 8))))) {
				LOG("M %d %d\n", mlen, off);
				match = curr_out - off;
				memcpy(curr_out, match, 8);
				memcpy(curr_out+8, match+8, 8);
				memcpy(curr_out+16, match+16, 2);
				curr_out += mlen;
				continue;
			}
			goto match;
		}

		if (likely(llen > 0)) {
			if (unlikely(llen == 15)) {
				if (unlikely(curr_in >= end - 15))
					return EIO;

				curr_in = decode_length(curr_in, &llen);
				llen += 15;
			}
			LOG("L %d\n", llen);
			if (unlikely((curr_in + llen) > end))
				return EIO;
			if (unlikely((curr_out + llen) > out_limit))
				return EOVERFLOW;
			memcpy(curr_out, curr_in, llen);
			curr_in += llen;
			curr_out += llen;
		}

		if (unlikely(off > (curr_out - buffer_out)))
			return EIO;

 match:
		if (unlikely(off == 0))
			break;

		if (likely(mlen < (15 + MIN_MATCH) && off >= mlen &&
		    (curr_out + (14 + MIN_MATCH)) <= out_limit)) {
			LOG("M %d %d\n", mlen, off);
			match = curr_out - off;
			memcpy(curr_out, match, 8);
			memcpy(curr_out+8, match+8, 8);
			memcpy(curr_out+16, match+16, 2);
			curr_out += mlen;
			continue;
		}

		if (likely(mlen == (15 + MIN_MATCH))) {
			if (unlikely(curr_in >= match_end))
				return EIO;

			curr_in = decode_length(curr_in, &mlen);
			mlen += 15 + MIN_MATCH;
		}

		LOG("M %d %d\n", mlen, off);
		if (unlikely((curr_out + mlen) > out_limit))
			return EOVERFLOW;

		match = curr_out - off;
		mend = curr_out + mlen;

		if (likely(mlen <= off)) {
			memcpy(curr_out, match, mlen);
			curr_out += mlen;
			continue;
		}

		if (off == 1) {
			c = *match;
			*curr_out++ = c;
			*curr_out++ = c;
			*curr_out++ = c;
			*curr_out++ = c;
			while (curr_out < mend)
				*curr_out++ = c;
			continue;
		}

		if (off == 2) {
			c = readmem16(match);
			writemem16(curr_out, c);
			curr_out += 2;
			writemem16(curr_out, c);
			curr_out += 2;
			while (curr_out < mend) {
				writemem16(curr_out, c);
				curr_out += 2;
				writemem16(curr_out, c);
				curr_out += 2;
			}
			curr_out = mend;
			continue;
		}

		if (off == 3) {
			unsigned char c1, c2, c3;
			c1 = *match;
			c2 = *(match+1);
			c3 = *(match+2);
			*curr_out++ = c1;
			*curr_out++ = c2;
			*curr_out++ = c3;
			*curr_out++ = c1;
			*curr_out++ = c2;
			*curr_out++ = c3;
			while (curr_out < mend) {
				*curr_out++ = c1;
				*curr_out++ = c2;
				*curr_out++ = c3;
			}
			curr_out = mend;
		}

		if (off == 4) {
			c = readmem32(match);
			writemem32(curr_out, c);
			curr_out += off;
			writemem32(curr_out, c);
			curr_out += off;
			while (curr_out < mend) {
				writemem32(curr_out, c);
				curr_out += off;
				writemem32(curr_out, c);
				curr_out += off;
			}
			curr_out = mend;
			continue;
		}

		if (off <= 8) {
			c = readmem64(match);
			writemem64(curr_out, c);
			curr_out += off;
			while (curr_out < mend) {
				writemem64(curr_out, c);
				curr_out += off;
			}
			curr_out = mend;
			continue;
		}

		memcpy(curr_out, match, 4);
		match += 4;
		curr_out += 4;
		while (curr_out < mend) {
			memcpy(curr_out, match, 8);
			match += 8;
			curr_out += 8;
		}
		curr_out = mend;
	}

	*size_out = curr_out - buffer_out;

	/* Finished without seeing end of stream? */
	if (off != 0)
		return EIO;

	return 0;
}
