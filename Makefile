CFLAGS=-Wall -Werror -Wcast-align -Wstrict-overflow -Wstrict-aliasing -Wextra -Wpedantic -Wshadow -O3 -march=native -falign-loops=4 # -DDEBUG=1

all:	lzm lzdata

lzm:	lzm.o lzmencode.o lzmdecode.o

lzm.o:	lzm.c lzm.h conf.h

lzmencode.o:	lzmencode.c lzm.h lzm_int.h conf.h mem.h

lzmdecode.o:	lzmdecode.c lzm.h lzm_int.h conf.h mem.h

lzdata: lzdata.o

lzdata.o: lzdata.c conf.h mem.h

clean:
	rm -f lzm lzdata *.o
