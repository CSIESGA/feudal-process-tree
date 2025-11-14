CC = gcc
CFLAGS = -Wall -O2 -w

all: monarch

monarch: monarch.c hw2.h
	$(CC) $(CFLAGS) monarch.c -o monarch

clean:
	rm -f monarch