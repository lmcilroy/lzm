#define HASH_ORDER_FAST		12
#define HASH_ORDER_MID		16
#define HASH_ORDER_HIGH		20
#define MAX_CHAIN_LENGTH	128
#define MIN_MATCH		4
#define MISS_ORDER		6
#define MAX_OFFSET_ORDER	28
#define MAX_OFFSET		(1 << MAX_OFFSET_ORDER)
#define MAX_OFFSET_MASK		(MAX_OFFSET - 1)
#define MEM_ALIGN		64

struct lzm_state {
	struct ht_entry *last_ht;
	struct ht_entry *chains;
	unsigned int hash_order;
	unsigned int hash_buckets;
	unsigned int chain_order;
	unsigned int chain_mask;
	unsigned int level;
	unsigned int format;
};
