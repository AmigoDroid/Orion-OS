
#include "usb_device.h"

static struct usb_device devices[USB_MAX_DEVICES];
static unsigned int device_count = 0;

void usb_devices_initialize(void)
{
    unsigned int i;

    device_count = 0;

    for (i = 0; i < USB_MAX_DEVICES; i++) {
        devices[i].address = 0;
        devices[i].device_class = 0;
        devices[i].subclass = 0;
        devices[i].protocol = 0;
        devices[i].vendor_id = 0;
        devices[i].product_id = 0;
        devices[i].configured = 0;
    }
}

unsigned int usb_device_count(void)
{
    return device_count;
}

int usb_device_get(unsigned int index, struct usb_device *out)
{
    if (out == 0 || index >= device_count)
        return 0;

    *out = devices[index];
    return 1;
}

int usb_device_register(const struct usb_device *device)
{
    if (device == 0 || device_count >= USB_MAX_DEVICES)
        return 0;

    devices[device_count++] = *device;
    return 1;
}
