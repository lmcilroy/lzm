#define CHUNK_SIZE	(4<<20)
#define likely(cond)	__builtin_expect((cond), 1)
#define unlikely(cond)	__builtin_expect((cond), 0)
#define true		1
#define false		0

#ifdef DEBUG
#define LOG(fmt, ...)	do { fprintf(stderr, fmt, __VA_ARGS__); } while (0)
#else
#define LOG(fmt, ...)
#endif
