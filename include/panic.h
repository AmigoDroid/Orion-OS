#ifndef ORION_PANIC_H
#define ORION_PANIC_H

/* Erro fatal do kernel: escreve a mensagem no log (0xE9) e na tela e
 * para a CPU. Se o proprio panic falhar (tela corrompida, por exemplo),
 * a segunda entrada so escreve no log e para. */
void panic(const char *fmt, ...)
    __attribute__((noreturn, format(printf, 1, 2)));

/* Cabecalho/rodape padrao usados tambem pelo handler de excecoes. */
void panic_begin(const char *title);
void panic_end(void) __attribute__((noreturn));
int  panic_in_progress(void);

#endif
