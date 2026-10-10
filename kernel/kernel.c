
typedef unsigned char  u8;
typedef unsigned short u16;

void kernel_main(void)
{
    volatile u16 *video = (volatile u16 *)0xB8000;

    const char *mensagem = "ORION OS - Kernel em C iniciado!";
    unsigned int i = 0;

    while (mensagem[i] != '\0') {
        video[i] = (u16)mensagem[i] | (u16)(0x0A << 8);
        i++;
    }

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
