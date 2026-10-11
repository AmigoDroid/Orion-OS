#ifndef ORION_GFD_H
#define ORION_GFD_H

/* OrionGFD - interface grafica do Orion OS (ver docs/ORIONGFD.md).
 *
 * As funcoes abaixo sao "hooks" fracos para quem usa o GFD (terminal.c,
 * keyboard.c e kernel.c). Assim o build do disquete (modo texto, sem gfd.o)
 * continua linkando: o simbolo ausente vale 0 e o hook e simplesmente ignorado.
 * Dentro de gfd.c (que define ORION_GFD_IMPL) elas sao funcoes normais. */

#ifdef ORION_GFD_IMPL
#define GFD_HOOK
#else
#define GFD_HOOK __attribute__((weak))
#endif

/* Chamado pelo entry do GRUB com o ponteiro Multiboot. Retorna 1 se o modo
 * grafico foi iniciado, 0 se nao ha framebuffer (o kernel segue em modo texto). */
int  gfd_init(unsigned int multiboot_address);

int  gfd_active(void);

/* Redesenha o que mudou. Chamar a cada volta do laco principal. */
GFD_HOOK void gfd_poll(void);

/* Recebe um byte do mouse PS/2 (keyboard.c repassa quando o bit AUX esta setado). */
GFD_HOOK int  gfd_mouse_feed(unsigned char byte);

/* Console da janela Terminal. Retornam 1 quando o GFD tratou a chamada. */
GFD_HOOK int  gfd_console_putchar(char c);
GFD_HOOK int  gfd_console_clear(void);

#endif
