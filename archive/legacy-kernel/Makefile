
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
PCI_O      = $(BUILD)/pci.o

USB_O            = $(BUILD)/usb.o
USB_CONTROLLER_O = $(BUILD)/usb_controller.o
USB_DEVICE_O     = $(BUILD)/usb_device.o

KERNEL_ELF = $(BUILD)/kernel.elf
KERNEL_BIN = $(BUILD)/kernel.bin
IMAGE      = $(BUILD)/orion.img

DATA_IMG = $(BUILD)/orion-data.img

# Arquivos exclusivos para o boot pelo GRUB
GRUB_ENTRY_O    = $(BUILD)/grub_entry.o
GRUB_KERNEL_ELF = $(BUILD)/kernel-grub.elf
GRUB_ISO        = $(BUILD)/orion-grub.iso
GRUB_ISO_DIR    = $(BUILD)/iso-grub

# Dependencias automaticas dos cabecalhos
DEPFLAGS = -MMD -MP

# Diretorios de cabecalhos
INCLUDES = -Iinclude \
           -Ikernel \
           -Idrivers/keyboard \
           -Idrivers/mouse \
           -Idrivers/ata \
           -Idrivers/pci \
           -Idrivers/usb \
           -Idrivers/usb/controllers \
           -Idrivers/usb/devices \
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

# ============================================================
# BOOTLOADER ORIGINAL
# ============================================================

$(BOOT): boot/boot.asm | $(BUILD)
	$(NASM) -f bin $< -o $@
	test "$$(stat -c %s $@)" -eq 512

$(STAGE2): boot/stage2.asm | $(BUILD)
	$(NASM) -f bin $< -o $@
	test "$$(stat -c %s $@)" -eq 1024

$(GRUB_ENTRY_O): boot/grub_entry.asm | $(BUILD)
	$(NASM) -f elf32 $< -o $@

# ============================================================
# OBJETOS DO KERNEL
# ============================================================

$(KERNEL_O): kernel/kernel.c kernel/kernel_includes.h | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(TERMINAL_O): kernel/terminal.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(KEYBOARD_O): drivers/keyboard/keyboard.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(MOUSE_O): drivers/mouse/mouse.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(LOG_O): kernel/log.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(ATA_O): drivers/ata/ata.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(FS_O): fs/fs.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(GFD_O): kernel/gfd.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# ============================================================
# PCI
# ============================================================

$(PCI_O): drivers/pci/pci.c drivers/pci/pci.h | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# ============================================================
# USB - NUCLEO
# ============================================================

$(USB_O): drivers/usb/usb.c drivers/usb/usb.h \
          drivers/pci/pci.h \
          drivers/usb/controllers/usb_controller.h \
          drivers/usb/devices/usb_device.h | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# ============================================================
# USB - IDENTIFICACAO DOS CONTROLADORES
# ============================================================

$(USB_CONTROLLER_O): drivers/usb/controllers/usb_controller.c \
                     drivers/usb/controllers/usb_controller.h | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# ============================================================
# USB - ESTRUTURA DOS DISPOSITIVOS
# ============================================================

$(USB_DEVICE_O): drivers/usb/devices/usb_device.c \
                 drivers/usb/devices/usb_device.h | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# ============================================================
# KERNEL ELF - BOOTLOADER ORIGINAL
# ============================================================

$(KERNEL_ELF): $(KERNEL_O) $(TERMINAL_O) $(KEYBOARD_O) \
               $(MOUSE_O) $(LOG_O) $(ATA_O) $(FS_O) $(PCI_O) \
               $(USB_O) $(USB_CONTROLLER_O) $(USB_DEVICE_O) \
               kernel/linker.ld
	$(CC) $(LDFLAGS) -o $@ \
		$(KERNEL_O) $(TERMINAL_O) $(KEYBOARD_O) \
		$(MOUSE_O) $(LOG_O) $(ATA_O) $(FS_O) $(PCI_O) \
		$(USB_O) $(USB_CONTROLLER_O) $(USB_DEVICE_O)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@size=$$(stat -c %s $@); \
	if [ "$$size" -gt 32768 ]; then \
		echo "ERRO: kernel excede o limite de 64 setores (32768 bytes)."; \
		rm -f $@; \
		exit 1; \
	fi

$(IMAGE): $(BOOT) $(STAGE2) $(KERNEL_BIN)
	cat $(BOOT) $(STAGE2) $(KERNEL_BIN) > $@
	truncate -s 1474560 $@
	@echo "Imagem original do Orion criada: $(IMAGE)"
	@stat -c 'Tamanho: %s bytes' $@

# ============================================================
# DISCO DE DADOS
# ============================================================

$(DATA_IMG): | $(BUILD)
	dd if=/dev/zero of=$@ bs=1M count=1 2>/dev/null

disk: $(DATA_IMG)

# ============================================================
# SERVIDOR WEB
# ============================================================

web:
	cd web && node server.js

# ============================================================
# LINKAGEM DO KERNEL GRUB
# ============================================================

$(GRUB_KERNEL_ELF): $(GRUB_ENTRY_O) $(KERNEL_O) \
                    $(TERMINAL_O) $(KEYBOARD_O) $(MOUSE_O) \
                    $(LOG_O) $(ATA_O) $(FS_O) $(GFD_O) $(PCI_O) \
                    $(USB_O) $(USB_CONTROLLER_O) $(USB_DEVICE_O) \
                    kernel/grub.ld
	$(CC) $(GRUB_LDFLAGS) -o $@ \
		$(GRUB_ENTRY_O) $(KERNEL_O) $(TERMINAL_O) \
		$(KEYBOARD_O) $(MOUSE_O) $(LOG_O) \
		$(ATA_O) $(FS_O) $(GFD_O) $(PCI_O) \
		$(USB_O) $(USB_CONTROLLER_O) $(USB_DEVICE_O)

# ============================================================
# ISO GRUB
# ============================================================

$(GRUB_ISO): $(GRUB_KERNEL_ELF) boot/iso-grub/boot/grub/grub.cfg
	mkdir -p $(GRUB_ISO_DIR)/boot/grub
	cp $(GRUB_KERNEL_ELF) $(GRUB_ISO_DIR)/boot/kernel-grub.elf
	cp boot/iso-grub/boot/grub/grub.cfg $(GRUB_ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(GRUB_ISO_DIR)
	@echo "ISO do Orion para GRUB criada: $@"

grub: $(GRUB_ISO)

# ============================================================
# EXECUCAO NO QEMU
# ============================================================

run: $(IMAGE) $(DATA_IMG)
	$(QEMU) -machine pc -m 128M \
		-drive format=raw,file=$(IMAGE),if=floppy \
		-drive format=raw,file=$(DATA_IMG),if=ide,index=0 \
		-boot a

run-grub: $(GRUB_ISO) $(DATA_IMG)
	$(QEMU) -machine pc -m 128M \
		-cdrom $(GRUB_ISO) \
		-drive format=raw,file=$(DATA_IMG),if=ide,index=0 \
		-boot d

# ============================================================
# DEPENDENCIAS AUTOMATICAS
# ============================================================

-include $(wildcard $(BUILD)/*.d)

# ============================================================
# LIMPEZA E RECOMPILACAO
# ============================================================

clean:
	find $(BUILD) -mindepth 1 \
		! -name orion-data.img \
		! -name .gitkeep -delete

rebuild: clean
	$(MAKE) all
