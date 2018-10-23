#define MEM_ACCESS_DIRECT	0
#define MEM_ACCESS_PACKED	0
#define MEM_ACCESS_MEMCPY	1

#if MEM_ACCESS_DIRECT

static inline unsigned short
readmem16(const void * const mem)
{
	return *(unsigned short *)mem;
}

static inline unsigned int
readmem32(const void * const mem)
{
	return *(unsigned int *)mem;
}

static inline unsigned long
readmem64(const void * const mem)
{
	return *(unsigned long *)mem;
}

static inline void
writemem16(void * const mem, const unsigned short val)
{
	*(unsigned short *)mem = val;
}

static inline void
writemem32(void * const mem, const unsigned int val)
{
	*(unsigned int *)mem = val;
}

static inline void
writemem64(void * const mem, const unsigned long val)
{
	*(unsigned long *)mem = val;
}

#endif

#if MEM_ACCESS_PACKED

union unalign {
	unsigned short u16;
	unsigned int u32;
	unsigned long u64;
} __attribute__((packed));

static inline unsigned short
readmem16(const void * const mem)
{
	const union unalign * const unalign = mem;
	return unalign->u16;
}

static inline unsigned int
readmem32(const void * const mem)
{
	const union unalign * const unalign = mem;
	return unalign->u32;
}

static inline unsigned long
readmem64(const void * const mem)
{
	const union unalign * const unalign = mem;
	return unalign->u64;
}

static inline void
writemem16(void * const mem, const unsigned short val)
{
	union unalign * const unalign = mem;
	unalign->u16 = val;
}

static inline void
writemem32(void * const mem, const unsigned int val)
{
	union unalign * const unalign = mem;
	unalign->u32 = val;
}

static inline void
writemem64(void * const mem, const unsigned long val)
{
	union unalign * const unalign = mem;
	unalign->u64 = val;
}

#endif

#if MEM_ACCESS_MEMCPY

static inline unsigned short
readmem16(const void * const mem)
{
	unsigned short val;
	memcpy(&val, mem, sizeof(val));
	return val;
}

static inline unsigned int
readmem32(const void * const mem)
{
	unsigned int val;
	memcpy(&val, mem, sizeof(val));
	return val;
}

static inline unsigned long
readmem64(const void * const mem)
{
	unsigned long val;
	memcpy(&val, mem, sizeof(val));
	return val;
}

static inline void
writemem16(void * const mem, const unsigned short val)
{
	memcpy(mem, &val, sizeof(val));
}

static inline void
writemem32(void * const mem, const unsigned int val)
{
	memcpy(mem, &val, sizeof(val));
}

static inline void
writemem64(void * const mem, const unsigned long val)
{
	memcpy(mem, &val, sizeof(val));
}

#endif

static inline void
memcopy16(void * const dst, const void * const src)
{
	writemem16(dst, readmem16(src));
}

static inline void
memcopy32(void * const dst, const void * const src)
{
	writemem32(dst, readmem32(src));
}

static inline void
memcopy64(void * const dst, const void * const src)
{
	writemem64(dst, readmem64(src));
}
