
NASM      = nasm
CC        = gcc
OBJCOPY   = objcopy
QEMU      = qemu-system-x86_64

BUILD     = build
BOOT      = $(BUILD)/boot.bin
STAGE2    = $(BUILD)/stage2.bin
KERNEL_O  = $(BUILD)/kernel.o
TERMINAL_O = $(BUILD)/terminal.o
KERNEL_ELF = $(BUILD)/kernel.elf
KERNEL_BIN = $(BUILD)/kernel.bin
IMAGE     = $(BUILD)/orion.img

CFLAGS = -m32 -ffreestanding -fno-pie \
         -fno-stack-protector \
         -fno-asynchronous-unwind-tables

LDFLAGS = -m32 -nostdlib -no-pie \
          -Wl,-T,kernel/linker.ld \
          -Wl,--build-id=none

.PHONY: all run clean rebuild

all: $(IMAGE)

$(BUILD):
	mkdir -p $(BUILD)

$(BOOT): boot/boot.asm | $(BUILD)
	$(NASM) -f bin $< -o $@
	test "$$(stat -c %s $@)" -eq 512

$(STAGE2): boot/stage2.asm | $(BUILD)
	$(NASM) -f bin $< -o $@
	test "$$(stat -c %s $@)" -eq 1024

$(KERNEL_O): kernel/kernel.c include/terminal.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(TERMINAL_O): kernel/terminal.c include/terminal.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(KERNEL_ELF): $(KERNEL_O) $(TERMINAL_O) kernel/linker.ld
	$(CC) $(LDFLAGS) -o $@ $(KERNEL_O) $(TERMINAL_O)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

$(IMAGE): $(BOOT) $(STAGE2) $(KERNEL_BIN)
	cat $(BOOT) $(STAGE2) $(KERNEL_BIN) > $@
	@echo "Imagem do Orion criada: $(IMAGE)"
	@stat -c 'Tamanho: %s bytes' $@

run: $(IMAGE)
	$(QEMU) -machine pc -m 128M \
		-drive format=raw,file=$(IMAGE),if=floppy \
		-boot a

clean:
	rm -f $(BUILD)/*

rebuild: clean
	$(MAKE) all
