
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
MOUSE_O    = $(BUILD)/mouse.o
LOG_O      = $(BUILD)/log.o
ATA_O      = $(BUILD)/ata.o
FS_O       = $(BUILD)/fs.o
GFD_O      = $(BUILD)/gfd.o
PCI_O       = $(BUILD)/pci.o

KERNEL_ELF = $(BUILD)/kernel.elf
KERNEL_BIN = $(BUILD)/kernel.bin
IMAGE      = $(BUILD)/orion.img

DATA_IMG = $(BUILD)/orion-data.img

# Arquivos exclusivos para o boot pelo GRUB
GRUB_ENTRY_O    = $(BUILD)/grub_entry.o
GRUB_KERNEL_ELF = $(BUILD)/kernel-grub.elf
GRUB_ISO        = $(BUILD)/orion-grub.iso
GRUB_ISO_DIR    = $(BUILD)/iso-grub

# Dependências automáticas dos cabeçalhos
DEPFLAGS = -MMD -MP

# Diretórios onde o compilador procura os cabeçalhos
INCLUDES = -Iinclude \
           -Ikernel \
           -Idrivers/keyboard \
           -Idrivers/mouse \
           -Idrivers/ata \
           -Ifs

CFLAGS = -m32 -ffreestanding -fno-pie \
         -fno-stack-protector \
         -fno-asynchronous-unwind-tables \
         $(INCLUDES)

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

# Kernel principal
$(KERNEL_O): kernel/kernel.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Descoberta de dispositivos PCI
$(PCI_O): drivers/pci/pci.c drivers/pci/pci.h | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Idrivers/pci -c $< -o $@

# Terminal
$(TERMINAL_O): kernel/terminal.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Teclado
$(KEYBOARD_O): drivers/keyboard/keyboard.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Mouse
$(MOUSE_O): drivers/mouse/mouse.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Logs
$(LOG_O): kernel/log.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Driver ATA
$(ATA_O): drivers/ata/ata.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Sistema de arquivos
$(FS_O): fs/fs.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Kernel ELF do bootloader original
$(KERNEL_ELF): $(KERNEL_O) $(TERMINAL_O) $(KEYBOARD_O) \
               $(MOUSE_O) $(LOG_O) $(ATA_O) $(FS_O) $(PCI_O) \
               kernel/linker.ld
	$(CC) $(LDFLAGS) -o $@ \
		$(KERNEL_O) $(TERMINAL_O) $(KEYBOARD_O) \
		$(MOUSE_O) $(LOG_O) $(ATA_O) $(FS_O) $(PCI_O)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@size=$$(stat -c %s $@); \
	if [ "$$size" -gt 32768 ]; then \
		echo "ERRO: kernel excede o limite de 64 setores (32768 bytes)."; \
		rm -f $@; \
		exit 1; \
	fi
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

# OrionGFD: interface gráfica
$(GFD_O): kernel/gfd.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Kernel ELF separado para o GRUB
$(GRUB_KERNEL_ELF): $(GRUB_ENTRY_O) $(KERNEL_O) \
                    $(TERMINAL_O) $(KEYBOARD_O) $(MOUSE_O) \
                    $(LOG_O) $(ATA_O) $(FS_O) $(GFD_O) $(PCI_O) \
                    kernel/grub.ld
	$(CC) $(GRUB_LDFLAGS) -o $@ \
		$(GRUB_ENTRY_O) $(KERNEL_O) $(TERMINAL_O) \
		$(KEYBOARD_O) $(MOUSE_O) $(LOG_O) \
		$(ATA_O) $(FS_O) $(GFD_O) $(PCI_O)

# ISO inicializável pelo GRUB
$(GRUB_ISO): $(GRUB_KERNEL_ELF) boot/iso-grub/boot/grub/grub.cfg
	mkdir -p $(GRUB_ISO_DIR)/boot/grub
	cp $(GRUB_KERNEL_ELF) $(GRUB_ISO_DIR)/boot/kernel-grub.elf
	cp boot/iso-grub/boot/grub/grub.cfg $(GRUB_ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(GRUB_ISO_DIR)
	@echo "ISO do Orion para GRUB criada: $@"

grub: $(GRUB_ISO)

# Executar a ISO do GRUB
run-grub: $(GRUB_ISO) $(DATA_IMG)
	$(QEMU) -machine pc -m 128M \
		-cdrom $(GRUB_ISO) \
		-drive format=raw,file=$(DATA_IMG),if=ide,index=0 \
		-boot d

# Carrega as dependências geradas automaticamente pelo GCC
-include $(wildcard $(BUILD)/*.d)

# Limpa arquivos de compilação, preservando o disco de dados
clean:
	find $(BUILD) -mindepth 1 ! -name orion-data.img ! -name .gitkeep -delete

rebuild: clean
	$(MAKE) all
