CC=gcc
CFLAGS=-Wall -lpthread

all: socket.c socket.h main.c
	$(CC) socket.c main.c -o variantcall $(CFLAGS)

.PHONY: clean

clean:
	rm variantcall
