#ifndef ORION_GFD_H
#define ORION_GFD_H

/* OrionGFD - interface grafica do Orion OS (ver docs/ORIONGFD.md).
 *
 * O GFD agora faz parte de TODO build do kernel (antes ele so existia no
 * kernel do GRUB e por isso as funcoes eram "weak"). Sem framebuffer
 * (boot em modo texto), gfd_init() devolve 0 e todas as funcoes abaixo
 * viram no-ops que devolvem 0: o terminal VGA de texto assume. */

/* Recebe o ponteiro Multiboot. Retorna 1 se o modo grafico foi iniciado. */
int  gfd_init(unsigned int multiboot_address);
int  gfd_active(void);

/* Selecao de fontes bitmap do OrionGFD. */
int gfd_set_font(unsigned int index);
unsigned int gfd_get_font(void);
unsigned int gfd_font_count(void);
const char *gfd_font_name(unsigned int index);
const char *gfd_font_key(unsigned int index);

/* Liga o buffer duplo (cena montada na RAM, copiada inteira para a tela).
 * Chamar DEPOIS de mm_init(). Devolve 1 se ligou; 0 se nao ha GFD ou
 * memoria (nesse caso o GFD continua funcionando, so que piscando). */
int  gfd_enable_backbuffer(void);

/* Redesenha o que mudou. Chamar a cada volta do laco principal. */
void gfd_poll(void);

/* Recebe um byte do mouse PS/2. Devolve 1 se o GFD consumiu o byte. */
int  gfd_mouse_feed(unsigned char byte);

/* Console da janela Terminal. Devolvem 1 quando o GFD tratou a chamada. */
int  gfd_console_putchar(char c);
int  gfd_console_clear(void);

#endif
