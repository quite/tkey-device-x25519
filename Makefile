OBJCOPY ?= llvm-objcopy

P := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
LIBDIR ?= $(P)/../tkey-libs

CC = clang

INCLUDE=$(LIBDIR)/include

# If you want the qemu_*() functions to print stuff on the QEMU debug port, add
# -DQEMU_DEBUG to these flags. Do this also in $(LIBDIR)/Makefile before
# building there.
CFLAGS = -target riscv32-unknown-none-elf -march=rv32iczmmul -mabi=ilp32 -mcmodel=medany \
   -static -std=gnu99 -O2 -ffast-math -fno-common -fno-builtin-printf \
   -fno-builtin-putchar -nostdlib -mno-relax -flto -g \
   -Wall -Werror=implicit-function-declaration \
   -I $(INCLUDE) -I $(LIBDIR)

LDFLAGS=-T $(LIBDIR)/app.lds -L $(LIBDIR) -lcrt0 -lcommon


.PHONY: all
all: x25519/app.bin check-x25519-hash

show-%-hash: %/app.bin
	cd $$(dirname $^) && sha512sum app.bin

check-x25519-hash: x25519/app.bin
	@(cd x25519; echo "file:$$(pwd)/app.bin hash:$$(sha512sum app.bin | cut -c1-16)… expected:$$(cut -c1-16 <app.bin.sha512)…"; sha512sum -cw app.bin.sha512)

x25519/app.bin: x25519/app.elf
	$(OBJCOPY) --input-target=elf32-littleriscv --output-target=binary $^ $@
	chmod a-x $@

X25519OBJS=x25519/main.o x25519/app_proto.o
x25519/app.elf: $(X25519OBJS)
	$(CC) $(CFLAGS) $(X25519OBJS) $(LDFLAGS) -L $(LIBDIR)/monocypher -lmonocypher -I $(LIBDIR) -o $@
$(X25519OBJS): x25519/app_proto.h

# .PHONY to let go-build handle deps and rebuilds
.PHONY: testx25519
testx25519: x25519/app.bin
	cp -af x25519/app.bin ./cmd/testx25519/
	go build ./cmd/testx25519

.PHONY: clean
clean:
	rm -f x25519/app.bin x25519/app.elf $(X25519OBJS) testx25519

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
	podman run --rm \
	--mount type=bind,source=$(CURDIR),target=/src \
	--mount type=bind,source=$(LIBDIR),target=/tkey-libs \
	-w /src -it tkey-apps-builder \
	make -j
