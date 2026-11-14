CLANG ?= clang
LLD ?= ld.lld
OUT ?= build
CFLAGS := --target=aarch64-none-elf -O2 -ffreestanding -nostdlib -fpie -fno-builtin -fno-stack-protector -mgeneral-regs-only -mstrict-align -Wall -Wextra -Werror -I../../Boot/include

.PHONY: all kernel boot clean
all: kernel
kernel: $(OUT)/kernel.elf

$(OUT):
	mkdir -p $@

$(OUT)/main.o: src/main.c | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/kernel.elf: $(OUT)/main.o kernel.ld
	$(LLD) -T kernel.ld --build-id=none --no-undefined $< -o $@

boot: kernel
	$(MAKE) -C ../.. boot CONFIG=$(CURDIR)/byter.cfg

clean:
	rm -rf $(OUT)
