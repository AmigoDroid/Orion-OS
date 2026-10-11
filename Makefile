
NASM      = nasm
CC        = gcc
OBJCOPY   = objcopy
QEMU      = qemu-system-x86_64

BUILD     = build
BOOT      = $(BUILD)/boot.bin
STAGE2    = $(BUILD)/stage2.bin

KERNEL_O   = $(BUILD)/kernel.o
TERMINAL_O = $(BUILD)/terminal.o
KEYBOARD_O = $(BUILD)/keyboard.o
LOG_O      = $(BUILD)/log.o
ATA_O      = $(BUILD)/ata.o
FS_O       = $(BUILD)/fs.o
GFD_O      = $(BUILD)/gfd.o

KERNEL_ELF = $(BUILD)/kernel.elf
KERNEL_BIN = $(BUILD)/kernel.bin
IMAGE      = $(BUILD)/orion.img

DATA_IMG = $(BUILD)/orion-data.img

# Arquivos exclusivos para o boot pelo GRUB
GRUB_ENTRY_O    = $(BUILD)/grub_entry.o
GRUB_KERNEL_ELF = $(BUILD)/kernel-grub.elf
GRUB_ISO         = $(BUILD)/orion-grub.iso
GRUB_ISO_DIR     = $(BUILD)/iso-grub

CFLAGS = -m32 -ffreestanding -fno-pie \
         -fno-stack-protector \
         -fno-asynchronous-unwind-tables

LDFLAGS = -m32 -nostdlib -no-pie \
          -Wl,-T,kernel/linker.ld \
          -Wl,--build-id=none

GRUB_LDFLAGS = -m32 -nostdlib -no-pie \
               -Wl,-T,kernel/grub.ld \
               -Wl,--build-id=none

.PHONY: all run run-grub clean rebuild disk web grub

all: $(IMAGE)

$(BUILD):
	mkdir -p $(BUILD)

# Bootloader original
$(BOOT): boot/boot.asm | $(BUILD)
	$(NASM) -f bin $< -o $@
	test "$$(stat -c %s $@)" -eq 512

# Segundo estágio do bootloader original
$(STAGE2): boot/stage2.asm | $(BUILD)
	$(NASM) -f bin $< -o $@
	test "$$(stat -c %s $@)" -eq 1024

# Objetos do kernel
$(KERNEL_O): kernel/kernel.c include/terminal.h include/keyboard.h include/ata.h include/fs.h include/gfd.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(TERMINAL_O): kernel/terminal.c include/terminal.h include/gfd.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(KEYBOARD_O): kernel/keyboard.c include/keyboard.h include/gfd.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(LOG_O): kernel/log.c include/log.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(ATA_O): kernel/ata.c include/ata.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(FS_O): kernel/fs.c include/fs.h include/ata.h include/terminal.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@


# Kernel ELF do bootloader original
$(KERNEL_ELF): $(KERNEL_O) $(TERMINAL_O) $(KEYBOARD_O) $(LOG_O) $(ATA_O) $(FS_O)  kernel/linker.ld
	$(CC) $(LDFLAGS) -o $@ $(KERNEL_O) $(TERMINAL_O) $(KEYBOARD_O) $(LOG_O) $(ATA_O) $(FS_O)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

# Imagem de disquete original
$(IMAGE): $(BOOT) $(STAGE2) $(KERNEL_BIN)
	cat $(BOOT) $(STAGE2) $(KERNEL_BIN) > $@
	truncate -s 1474560 $@
	@echo "Imagem original do Orion criada: $(IMAGE)"
	@stat -c 'Tamanho: %s bytes' $@

# Disco de dados: só cria se ainda não existir
$(DATA_IMG): | $(BUILD)
	dd if=/dev/zero of=$@ bs=1M count=1 2>/dev/null

disk: $(DATA_IMG)

# Servidor web do projeto
web:
	cd web && node server.js

# Executar pelo bootloader original
run: $(IMAGE) $(DATA_IMG)
	$(QEMU) -machine pc -m 128M \
		-drive format=raw,file=$(IMAGE),if=floppy \
		-drive format=raw,file=$(DATA_IMG),if=ide,index=0 \
		-boot a

# Entrada Multiboot do GRUB
$(GRUB_ENTRY_O): boot/grub_entry.asm | $(BUILD)
	$(NASM) -f elf32 $< -o $@

# OrionGFD (interface grafica): so entra no build do GRUB
$(GFD_O): kernel/gfd.c include/gfd.h include/gfd_font.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

# Kernel ELF separado para o GRUB
$(GRUB_KERNEL_ELF): $(GRUB_ENTRY_O) $(KERNEL_O) $(TERMINAL_O) $(KEYBOARD_O) $(LOG_O) $(ATA_O) $(FS_O) $(GFD_O) kernel/grub.ld
	$(CC) $(GRUB_LDFLAGS) -o $@ \
		$(GRUB_ENTRY_O) $(KERNEL_O) $(TERMINAL_O) \
		$(KEYBOARD_O) $(LOG_O) $(ATA_O) $(FS_O) $(GFD_O)

# ISO inicializável pelo GRUB
$(GRUB_ISO): $(GRUB_KERNEL_ELF) boot/iso-grub/boot/grub/grub.cfg
	mkdir -p $(GRUB_ISO_DIR)/boot/grub
	cp $(GRUB_KERNEL_ELF) $(GRUB_ISO_DIR)/boot/kernel-grub.elf
	cp boot/iso-grub/boot/grub/grub.cfg $(GRUB_ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(GRUB_ISO_DIR)
	@echo "ISO do Orion para GRUB criada: $@"

grub: $(GRUB_ISO)

# Executar a ISO do GRUB (com o OrionGFD)
run-grub: $(GRUB_ISO) $(DATA_IMG)
	$(QEMU) -machine pc -m 128M \
		-cdrom $(GRUB_ISO) \
		-drive format=raw,file=$(DATA_IMG),if=ide,index=0 \
		-boot d

# Limpa arquivos de compilação, preservando o disco de dados
clean:
	find $(BUILD) -mindepth 1 ! -name orion-data.img ! -name .gitkeep -delete

rebuild: clean
	$(MAKE) all
