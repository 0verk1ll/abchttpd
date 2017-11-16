CC=clang
CFLAGS=-Wall -Wextra -pedantic -g
OUTNAME=http
SRCS=http.c hash.c subr.c

all:
	$(CC) $(CFLAGS) -o $(OUTNAME) $(SRCS)

clean:
	rm *.o
