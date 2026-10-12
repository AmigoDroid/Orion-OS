#ifndef ORION_MULTIBOOT_H
#define ORION_MULTIBOOT_H

/* Contrato de boot do Orion: tanto o GRUB quanto o bootloader proprio
 * (boot/stage2.asm) entregam o kernel com
 *     EAX = MULTIBOOT_BOOTLOADER_MAGIC
 *     EBX = ponteiro para mb_info_t
 * Assim existe UM unico caminho de entrada (arch/i386/entry.asm). */

#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002u

#define MB_FLAG_MEM          (1u << 0)   /* mem_lower / mem_upper validos   */
#define MB_FLAG_BOOTDEV      (1u << 1)
#define MB_FLAG_CMDLINE      (1u << 2)
#define MB_FLAG_MODS         (1u << 3)
#define MB_FLAG_MMAP         (1u << 6)   /* mapa de memoria (E820)          */
#define MB_FLAG_LOADER_NAME  (1u << 9)
#define MB_FLAG_FRAMEBUFFER  (1u << 12)

#define MB_MMAP_AVAILABLE    1u          /* demais tipos = reservado        */

typedef struct {
    uint32_t flags;
    uint32_t mem_lower, mem_upper, boot_device, cmdline;
    uint32_t mods_count, mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length, mmap_addr;
    uint32_t drives_length, drives_addr;
    uint32_t config_table, boot_loader_name, apm_table;
    uint32_t vbe_control_info, vbe_mode_info;
    uint16_t vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
    uint32_t fb_addr_lo, fb_addr_hi;
    uint32_t fb_pitch, fb_width, fb_height;
    uint8_t  fb_bpp, fb_type;
    uint8_t  pad[2];
    uint8_t  color_info[6];
} __attribute__((packed)) mb_info_t;

/* Cada entrada do mapa: `size` NAO conta os 4 bytes do proprio campo. */
typedef struct {
    uint32_t size;
    uint32_t base_lo, base_hi;
    uint32_t len_lo, len_hi;
    uint32_t type;
} __attribute__((packed)) mb_mmap_entry_t;

typedef struct {
    uint32_t start, end, mod_string, reserved;
} __attribute__((packed)) mb_module_t;

_Static_assert(__builtin_offsetof(mb_info_t, mmap_length) == 44, "mb_info: mmap");
_Static_assert(__builtin_offsetof(mb_info_t, boot_loader_name) == 64, "mb_info: loader");
_Static_assert(__builtin_offsetof(mb_info_t, fb_addr_lo) == 88, "mb_info: framebuffer");
_Static_assert(__builtin_offsetof(mb_info_t, color_info) == 112, "mb_info: color_info");

#endif
