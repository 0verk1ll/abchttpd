# SPDX-FileCopyrightText: 2021 2017 rdci
#
# SPDX-License-Identifier: MIT

CC=clang
CFLAGS=-Wall -Wextra -pedantic -g
OUTNAME=http
SRCS=http.c hash.c subr.c

all:
	$(CC) $(CFLAGS) -o $(OUTNAME) $(SRCS)

clean:
	rm *.o
