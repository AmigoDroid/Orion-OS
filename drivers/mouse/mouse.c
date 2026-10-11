#include "mouse.h"
#include "gfd.h"

typedef unsigned char u8;
static inline u8 inb(unsigned short port)
{
u8 value;
asm volatile (
"inb %1, %0"
: "=a"(value)
: "Nd"(port)
);
return value;
}
void mouse_initialize(void)
{
/* A inicialização do controlador PS/2
deverá ser implementada aqui. */
}
void mouse_poll(void)
{
u8 status = inb(0x64);
/* Só lê quando há um byte disponível
e ele pertence ao mouse. */
if ((status & 1) == 0 || (status & 0x20) == 0) {
return;
}
u8 data = inb(0x60);
if (gfd_mouse_feed) {
gfd_mouse_feed(data);
}
}
