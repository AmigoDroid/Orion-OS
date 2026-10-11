
NASM      = nasm
CC        = gcc
OBJCOPY   = objcopy
QEMU      = qemu-system-x86_64

BUILD     = build
BOOT      = $(BUILD)/boot.bin
STAGE2    = $(BUILD)/stage2.bin
KERNEL_O  = $(BUILD)/kernel.o
TERMINAL_O = $(BUILD)/terminal.o
KEYBOARD_O = $(BUILD)/keyboard.o
LOG_O = $(BUILD)/log.o
ATA_O = $(BUILD)/ata.o
FS_O = $(BUILD)/fs.o
KERNEL_ELF = $(BUILD)/kernel.elf
KERNEL_BIN = $(BUILD)/kernel.bin
IMAGE     = $(BUILD)/orion.img

CFLAGS = -m32 -ffreestanding -fno-pie \
         -fno-stack-protector \
         -fno-asynchronous-unwind-tables

LDFLAGS = -m32 -nostdlib -no-pie \
          -Wl,-T,kernel/linker.ld \
          -Wl,--build-id=none

.PHONY: all run clean rebuild disk web

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

$(KEYBOARD_O): kernel/keyboard.c include/keyboard.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(LOG_O): kernel/log.c include/log.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(ATA_O): kernel/ata.c include/ata.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(FS_O): kernel/fs.c include/fs.h include/ata.h include/terminal.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(KERNEL_ELF): $(KERNEL_O) $(TERMINAL_O) $(KEYBOARD_O) $(LOG_O) $(ATA_O) $(FS_O) kernel/linker.ld
	$(CC) $(LDFLAGS) -o $@ $(KERNEL_O) $(TERMINAL_O) $(KEYBOARD_O) $(LOG_O) $(ATA_O) $(FS_O)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

$(IMAGE): $(BOOT) $(STAGE2) $(KERNEL_BIN)
	cat $(BOOT) $(STAGE2) $(KERNEL_BIN) > $@
	truncate -s 1474560 $@
	@echo "Imagem do Orion criada: $(IMAGE)"
	@stat -c 'Tamanho: %s bytes' $@

DATA_IMG = $(BUILD)/orion-data.img

# Disco de dados do OrionFS (1 MB). Nao e apagado pelo make clean.
$(DATA_IMG): | $(BUILD)
	dd if=/dev/zero of=$@ bs=1M count=1 2>/dev/null

disk: $(DATA_IMG)

web:
	cd web && node server.js

run: $(IMAGE) $(DATA_IMG)
	$(QEMU) -machine pc -m 128M \
		-drive format=raw,file=$(IMAGE),if=floppy \
		-drive format=raw,file=$(BUILD)/orion-data.img,if=ide,index=0 \
		-boot a
clean:
	find $(BUILD) -mindepth 1 ! -name orion-data.img ! -name .gitkeep -delete

rebuild: clean
	$(MAKE) all
