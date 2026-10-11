
#include "usb_controller.h"

enum usb_controller_type usb_controller_detect(
    unsigned char class_code,
    unsigned char subclass,
    unsigned char prog_if)
{
    if (class_code != 0x0C || subclass != 0x03)
        return USB_CONTROLLER_UNKNOWN;

    switch (prog_if) {
        case 0x00:
            return USB_CONTROLLER_UHCI;

        case 0x10:
            return USB_CONTROLLER_OHCI;

        case 0x20:
            return USB_CONTROLLER_EHCI;

        case 0x30:
            return USB_CONTROLLER_XHCI;

        default:
            return USB_CONTROLLER_UNKNOWN;
    }
}

const char *usb_controller_name(enum usb_controller_type type)
{
    switch (type) {
        case USB_CONTROLLER_UHCI:
            return "UHCI";

        case USB_CONTROLLER_OHCI:
            return "OHCI";

        case USB_CONTROLLER_EHCI:
            return "EHCI";

        case USB_CONTROLLER_XHCI:
            return "xHCI";

        default:
            return "USB desconhecido";
    }
}
