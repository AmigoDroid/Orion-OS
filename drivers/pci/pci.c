#include "pci.h"
#include "terminal.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PCI_MAX_DEVICES    256

struct pci_device {
    unsigned char bus;
    unsigned char slot;
    unsigned char function;
    unsigned short vendor_id;
    unsigned short device_id;
    unsigned char class_code;
    unsigned char subclass;
    unsigned char prog_if;
    unsigned char header_type;
};

static struct pci_device_info devices[PCI_MAX_DEVICES];
static unsigned int stored_devices = 0;
static unsigned int total_devices = 0;

static inline void outl(unsigned short port, unsigned int value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned int inl(unsigned short port)
{
    unsigned int value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static unsigned int pci_read_config(
    unsigned char bus,
    unsigned char slot,
    unsigned char function,
    unsigned char offset)
{
    unsigned int address;

    address = 0x80000000u
        | ((unsigned int)bus << 16)
        | ((unsigned int)slot << 11)
        | ((unsigned int)function << 8)
        | (offset & 0xFC);

    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_register_device(
    unsigned char bus,
    unsigned char slot,
    unsigned char function)
{
    unsigned int id;
    unsigned int class_info;
    unsigned int header_info;
    struct pci_device_info *dev;

    id = pci_read_config(bus, slot, function, 0x00);

    if ((id & 0xFFFF) == 0xFFFF)
        return;

    class_info = pci_read_config(bus, slot, function, 0x08);
    header_info = pci_read_config(bus, slot, function, 0x0C);

    total_devices++;

    if (stored_devices >= PCI_MAX_DEVICES)
        return;

    dev = &devices[stored_devices++];

    dev->bus = bus;
    dev->slot = slot;
    dev->function = function;
    dev->vendor_id = id & 0xFFFF;
    dev->device_id = (id >> 16) & 0xFFFF;
    dev->class_code = (class_info >> 24) & 0xFF;
    dev->subclass = (class_info >> 16) & 0xFF;
    dev->prog_if = (class_info >> 8) & 0xFF;
    dev->header_type = (header_info >> 16) & 0xFF;
}

static void print_hex(unsigned int value, unsigned int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    char output[8];
    unsigned int i;

    if (digits > sizeof(output))
        digits = sizeof(output);

    for (i = 0; i < digits; i++) {
        unsigned int shift = (digits - 1 - i) * 4;
        output[i] = hex[(value >> shift) & 0xF];
    }

    for (i = 0; i < digits; i++)
        terminal_putchar(output[i]);
}

void pci_initialize(void)
{
    unsigned int bus;
    unsigned int slot;
    unsigned int function;

    stored_devices = 0;
    total_devices = 0;

    terminal_writestring(" Procurando dispositivos PCI...\n");

    /*
     * Mecanismo de configuracao PCI legado.
     * Apenas le os registros de configuracao.
     */
    for (bus = 0; bus < 256; bus++) {
        for (slot = 0; slot < 32; slot++) {
            unsigned int id;
            unsigned int header;

            id = pci_read_config(
                (unsigned char)bus,
                (unsigned char)slot,
                0,
                0x00);

            if ((id & 0xFFFF) == 0xFFFF)
                continue;

            pci_register_device(
                (unsigned char)bus,
                (unsigned char)slot,
                0);

            header = pci_read_config(
                (unsigned char)bus,
                (unsigned char)slot,
                0,
                0x0C);

            if (((header >> 23) & 1) != 0) {
                for (function = 1; function < 8; function++) {
                    pci_register_device(
                        (unsigned char)bus,
                        (unsigned char)slot,
                        (unsigned char)function);
                }
            }
        }
    }

    terminal_writestring(" Dispositivos PCI encontrados: ");
    print_hex(total_devices, 4);
    terminal_writestring("\n");
}

void pci_list_devices(void)
{
    unsigned int i;

    terminal_writestring("\n=== DISPOSITIVOS PCI ===\n");

    if (total_devices == 0) {
        terminal_writestring(
            "Nenhum dispositivo encontrado ou barramento indisponivel.\n");
        return;
    }

    for (i = 0; i < stored_devices; i++) {
        struct pci_device_info *dev = &devices[i];

        terminal_writestring("\nPCI ");
        print_hex(dev->bus, 2);
        terminal_putchar(':');
        print_hex(dev->slot, 2);
        terminal_putchar('.');
        print_hex(dev->function, 1);

        terminal_writestring("  Fabricante: ");
        print_hex(dev->vendor_id, 4);

        terminal_writestring("  Dispositivo: ");
        print_hex(dev->device_id, 4);

        terminal_writestring("\n Classe: ");
        print_hex(dev->class_code, 2);

        terminal_writestring("  Subclasse: ");
        print_hex(dev->subclass, 2);

        terminal_writestring("  ProgIF: ");
        print_hex(dev->prog_if, 2);

        terminal_writestring(
            "\n Driver PCI: nao registrado neste subsistema\n");
    }

    if (total_devices > stored_devices) {
        terminal_writestring(
            "\nAviso: a lista atingiu o limite de armazenamento.\n");
    }

    terminal_writestring("\nTotal encontrado: ");
    print_hex(total_devices, 4);
    terminal_writestring("\n");
}

unsigned int pci_device_count(void)
{
    return stored_devices;
}

int pci_get_device(unsigned int index, struct pci_device_info *out)
{
    if (out == 0 || index >= stored_devices)
        return 0;

    *out = devices[index];
    return 1;
}

