
#include "usb.h"
#include "usb_controller.h"
#include "usb_device.h"
#include "../pci/pci.h"
#include "terminal.h"

static unsigned int detected_controllers = 0;

static void usb_print_hex(unsigned int value, unsigned int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned int i;

    for (i = 0; i < digits; i++) {
        unsigned int shift = (digits - 1 - i) * 4;
        terminal_putchar(hex[(value >> shift) & 0x0F]);
    }
}

void usb_initialize(void)
{
    unsigned int i;
    struct pci_device_info pci;
    enum usb_controller_type type;

    detected_controllers = 0;
    usb_devices_initialize();

    terminal_writestring("\n=== SUBSISTEMA USB ORION ===\n");
    terminal_writestring("Procurando controladores USB via PCI...\n");

    for (i = 0; i < pci_device_count(); i++) {
        if (!pci_get_device(i, &pci))
            continue;

        type = usb_controller_detect(
            pci.class_code,
            pci.subclass,
            pci.prog_if
        );

        if (type == USB_CONTROLLER_UNKNOWN)
            continue;

        detected_controllers++;

        terminal_writestring("Controlador ");
        terminal_writestring(usb_controller_name(type));

        terminal_writestring(" PCI ");
        usb_print_hex(pci.bus, 2);
        terminal_putchar(':');
        usb_print_hex(pci.slot, 2);
        terminal_putchar('.');
        usb_print_hex(pci.function, 1);

        terminal_writestring(" VID=");
        usb_print_hex(pci.vendor_id, 4);

        terminal_writestring(" DID=");
        usb_print_hex(pci.device_id, 4);

        terminal_writestring("\n");
    }

    terminal_writestring("Controladores USB encontrados: ");
    usb_print_hex(detected_controllers, 2);
    terminal_writestring("\n");

    terminal_writestring(
        "Aviso: comunicacao USB ainda nao implementada.\n"
    );
}

void usb_list_controllers(void)
{
    terminal_writestring("Controladores USB detectados: ");
    usb_print_hex(detected_controllers, 2);
    terminal_writestring("\n");
}
