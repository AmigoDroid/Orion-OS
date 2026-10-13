#include "display.h"
#include "kprintf.h"
#include <stdint.h>

#define DISPLAY_PREFERRED_WIDTH  1280u
#define DISPLAY_PREFERRED_HEIGHT 720u
#define DISPLAY_PREFERRED_BPP    32u

void display_report(const mb_info_t *info)
{
    if (!info) {
        kprintf("[ODM] Erro: informacoes Multiboot ausentes.\n");
        return;
    }

    if (!(info->flags & MB_FLAG_FRAMEBUFFER)) {
        kprintf("[ODM] Nenhum framebuffer foi fornecido.\n");
        kprintf("[ODM] O kernel pode usar o terminal VGA.\n");
        return;
    }

    if (info->fb_addr_hi != 0 || info->fb_addr_lo == 0) {
        kprintf("[ODM] Endereco de framebuffer nao suportado.\n");
        return;
    }

    if (info->fb_width == 0 ||
        info->fb_height == 0 ||
        info->fb_bpp != 32 ||
        info->fb_type != 1 ||
        info->fb_pitch < info->fb_width * 4u) {
        kprintf("[ODM] Framebuffer invalido ou incompatível.\n");
        return;
    }

    kprintf("[ODM] Resolucao detectada: %ux%ux%u\n",
            info->fb_width,
            info->fb_height,
            (uint32_t)info->fb_bpp);

    kprintf("[ODM] Pitch: %u bytes por linha.\n",
            info->fb_pitch);

    kprintf("[ODM] Framebuffer: 0x%08x\n",
            info->fb_addr_lo);

    if (info->fb_width == DISPLAY_PREFERRED_WIDTH &&
        info->fb_height == DISPLAY_PREFERRED_HEIGHT &&
        info->fb_bpp == DISPLAY_PREFERRED_BPP) {
        kprintf("[ODM] Modo preferido 1280x720 ativo.\n");
    } else {
        kprintf("[ODM] Modo preferido: 1280x720x32.\n");
        kprintf("[ODM] O modo solicitado difere do modo recebido.\n");
        kprintf("[ODM] A troca de resolucao ainda nao foi implementada.\n");
    }
}
