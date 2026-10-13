#include "panic.h"
#include "kprintf.h"
#include "terminal.h"
#include "gfd.h"
#include "io.h"
#include "log.h"

static volatile int panic_depth;

int panic_in_progress(void)
{
    return panic_depth != 0;
}

/* Depth 0 -> primeira falha: tela + log.
 * Depth >= 1 -> falha dentro do panic: so log, depois para. */
void panic_begin(const char *title)
{
    panic_depth++;

    if (panic_depth > 1) {
        log_write("\n[PANIC DUPLO] ");
        log_write(title);
        log_putc('\n');
        cpu_halt_forever();
    }

    terminal_setcolor(0x4F);                 /* branco sobre vermelho (VGA) */
    kprintf("\n\n*** ORION PANIC: %s ***\n", title);
}

void panic_end(void)
{
    kprintf("\nSistema parado. Reinicie a maquina.\n");
    gfd_poll();                              /* garante que a tela mostre */
    cpu_halt_forever();
    for (;;) { }
}

void panic(const char *fmt, ...)
{
    va_list ap;

    panic_begin("erro fatal");
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    kprintf("\n");
    panic_end();
}
