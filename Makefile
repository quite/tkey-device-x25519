OBJCOPY ?= llvm-objcopy

P := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
LIBDIR ?= $(P)/../tkey-libs

CC = clang

INCLUDE=$(LIBDIR)/include

# If you want libcommon's qemu_puts() et cetera to output something on our QEMU
# debug port, remove -DNODEBUG below
CFLAGS = -target riscv32-unknown-none-elf -march=rv32iczmmul -mabi=ilp32 -mcmodel=medany \
   -static -std=gnu99 -O2 -ffast-math -fno-common -fno-builtin-printf \
   -fno-builtin-putchar -nostdlib -mno-relax -flto -g \
   -Wall -Werror=implicit-function-declaration \
   -I $(INCLUDE) -I $(LIBDIR)  \
   -DNODEBUG

AS = clang
ASFLAGS = -target riscv32-unknown-none-elf -march=rv32iczmmul -mabi=ilp32 -mcmodel=medany -mno-relax

LDFLAGS=-T $(LIBDIR)/app.lds -L $(LIBDIR)/libcommon/ -lcommon -L $(LIBDIR)/libcrt0/ -lcrt0

RM=/bin/rm


.PHONY: all
all: testx25519 x25519/app.bin check-x25519-hash

# Turn elf into bin for device
%.bin: %.elf
	$(OBJCOPY) --input-target=elf32-littleriscv --output-target=binary $^ $@
	chmod a-x $@

show-%-hash: %/app.bin
	cd $$(dirname $^) && sha512sum app.bin

check-x25519-hash: x25519/app.bin
	cd x25519 && sha512sum -c app.bin.sha512

X25519OBJS=x25519/main.o x25519/app_proto.o
x25519/app.elf: $(X25519OBJS)
	$(CC) $(CFLAGS) $(X25519OBJS) $(LDFLAGS) -L $(LIBDIR)/monocypher -lmonocypher -I $(LIBDIR) -o $@
$(X25519OBJS): $(INCLUDE)/tk1_mem.h x25519/app_proto.h

# .PHONY to let go-build handle deps and rebuilds
.PHONY: testx25519
testx25519: x25519/app.bin
	cp x25519/app.bin ./cmd/testx25519/
	go build ./cmd/testx25519

.PHONY: clean
clean:
	$(RM) -f x25519/app.bin x25519/app.elf $(X25519OBJS) testx25519

# Uses ../.clang-format
FMTFILES=x25519/*.[ch]

.PHONY: fmt
fmt:
	clang-format --dry-run --ferror-limit=0 $(FMTFILES)
	clang-format --verbose -i $(FMTFILES)
.PHONY: checkfmt
checkfmt:
	clang-format --dry-run --ferror-limit=0 --Werror $(FMTFILES)

.PHONY: podman
podman:
	podman run --rm --mount type=bind,source=$(CURDIR),target=/src --mount type=bind,source=$(LIBDIR),target=/tkey-libs -w /src -it ghcr.io/tillitis/tkey-builder:2 make -j
