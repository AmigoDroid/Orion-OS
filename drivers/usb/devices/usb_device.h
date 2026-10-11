
#ifndef ORION_USB_DEVICE_H
#define ORION_USB_DEVICE_H

#define USB_MAX_DEVICES 32

struct usb_device {
    unsigned char address;
    unsigned char device_class;
    unsigned char subclass;
    unsigned char protocol;
    unsigned short vendor_id;
    unsigned short product_id;
    unsigned char configured;
};

void usb_devices_initialize(void);
unsigned int usb_device_count(void);
int usb_device_get(unsigned int index, struct usb_device *out);
int usb_device_register(const struct usb_device *device);

#endif
