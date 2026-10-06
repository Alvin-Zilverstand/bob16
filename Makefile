CC ?= gcc
CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -Werror
.PHONY: all c-os test

all:
	$(CC) $(CFLAGS) tools/build.c -o build-tool
	./build-tool

c-os: all
	./bob --boot build/kernel.b16

test: all
	./build-tool --test
