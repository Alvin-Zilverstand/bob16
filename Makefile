CC ?= gcc
BOB64_CC ?= x86_64-w64-mingw32-gcc
CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -Werror
.PHONY: all c-os test bob64 bob64-test bob64-handoff bob64-handoff-test bob64cc bob64-app-test

all:
	$(CC) $(CFLAGS) tools/build.c -o build-tool
	./build-tool

c-os: all
	./bob --boot build/kernel.b16

test: all
	./build-tool --test

# x86-64 UEFI build, isolated from the bob16/bob32 emulator targets.
bob64:
	$(CC) $(CFLAGS) tools/build.c -o build-tool
	BOB64_CC="$(BOB64_CC)" ./build-tool --bob64

bob64-test: bob64
	BOB64_CC="$(BOB64_CC)" ./build-tool --bob64-test

# Opt-in destructive-to-firmware-services kernel handoff build.
bob64-handoff:
	$(CC) $(CFLAGS) tools/build.c -o build-tool
	BOB64_CC="$(BOB64_CC)" ./build-tool --bob64-handoff

bob64-handoff-test:
	$(CC) $(CFLAGS) tools/build.c -o build-tool
	BOB64_CC="$(BOB64_CC)" ./build-tool --bob64-handoff-test

bob64cc:
	$(CC) $(CFLAGS) tools/bob64cc.c -o bob64cc

bob64-app-test: bob64-test bob64cc
	BOB64_CC="$(BOB64_CC)" ./bob64cc apps/bob64_smoke.c build/bob64-app/smoke.b64e
	./build/test_bob64 build/bob64/EFI/BOOT/BOOTX64.EFI build/bob64-app/smoke.b64e
