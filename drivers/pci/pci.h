
#ifndef ORION_PCI_H
#define ORION_PCI_H

struct pci_device_info {
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

void pci_initialize(void);
void pci_list_devices(void);

unsigned int pci_device_count(void);
int pci_get_device(unsigned int index, struct pci_device_info *out);

#endif
