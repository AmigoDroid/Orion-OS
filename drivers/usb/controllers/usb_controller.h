
#ifndef ORION_USB_CONTROLLER_H
#define ORION_USB_CONTROLLER_H

enum usb_controller_type {
    USB_CONTROLLER_UNKNOWN = 0,
    USB_CONTROLLER_UHCI,
    USB_CONTROLLER_OHCI,
    USB_CONTROLLER_EHCI,
    USB_CONTROLLER_XHCI
};

enum usb_controller_type usb_controller_detect(
    unsigned char class_code,
    unsigned char subclass,
    unsigned char prog_if
);

const char *usb_controller_name(enum usb_controller_type type);

#endif

