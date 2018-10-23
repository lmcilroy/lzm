#ifdef __cplusplus
extern "C" {
#endif

#define SUFFIX		".lzm"
#define HEADER_VALUE	0x314D5A4C

#define LZM_LEVEL_0	0
#define LZM_LEVEL_1	1
#define LZM_LEVEL_2	2
#define LZM_LEVEL_3	3
#define LZM_LEVEL_4	4
#define LZM_LEVEL_5	5
#define LZM_LEVEL_6	6
#define LZM_LEVEL_COUNT	7

#define LZM_LEVEL_DEF	0xFFFFFFFF
#define LZM_LEVEL_NONE	LZM_LEVEL_0
#define LZM_LEVEL_FAST	LZM_LEVEL_1

#define LZM_FORMAT_1	1

struct lzm_state;

unsigned int lzm_compressed_size(
    const unsigned int);

unsigned int lzm_encode_init(
    struct lzm_state ** const state,
    const unsigned int format,
    const unsigned int level);

unsigned int lzm_encode(
    const struct lzm_state * const state,
    const unsigned char * const buffer_in,
    const unsigned int size_in,
    unsigned char * const buffer_out,
    unsigned int *size_out);

unsigned int lzm_encode_finish(
    const struct lzm_state * const state);

unsigned int lzm_decode_init(
    struct lzm_state ** const state,
    const unsigned int format);

unsigned int lzm_decode(
    const struct lzm_state * const state,
    const unsigned char * const buffer_in,
    const unsigned int size_in,
    unsigned char * const buffer_out,
    unsigned int *size_out);

unsigned int lzm_decode_finish(
    const struct lzm_state * const state);

#ifdef __cplusplus
}
#endif
