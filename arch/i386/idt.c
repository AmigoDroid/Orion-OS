#include "idt.h"
#include "gdt.h"
#include "panic.h"
#include "kprintf.h"
#include "log.h"
#include "io.h"

typedef struct {
    uint16_t offset_lo, selector;
    uint8_t  zero, type_attr;
    uint16_t offset_hi;
} __attribute__((packed)) idt_gate_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

extern uint32_t isr_stub_table[32];
extern char stack_guard[], stack_bottom[], stack_top[];

static idt_gate_t idt[256];
static idt_ptr_t  idt_ptr;

static const char *const exception_name[32] = {
    "divisao por zero (#DE)", "debug (#DB)", "NMI", "breakpoint (#BP)",
    "overflow (#OF)", "limite de array (#BR)", "opcode invalido (#UD)",
    "FPU indisponivel (#NM)", "double fault (#DF)", "segmento FPU",
    "TSS invalido (#TS)", "segmento ausente (#NP)", "pilha (#SS)",
    "protecao geral (#GP)", "page fault (#PF)", "reservado",
    "erro de FPU (#MF)", "alinhamento (#AC)", "machine check (#MC)",
    "SIMD (#XM)", "virtualizacao (#VE)", "protecao de fluxo (#CP)",
    "reservado", "reservado", "reservado", "reservado", "reservado",
    "reservado", "hypervisor (#HV)", "VMM (#VC)", "seguranca (#SX)",
    "reservado"
};

static void set_gate(int v, uint32_t handler, uint16_t sel, uint8_t attr)
{
    idt[v].offset_lo = handler & 0xFFFF;
    idt[v].selector  = sel;
    idt[v].zero      = 0;
    idt[v].type_attr = attr;
    idt[v].offset_hi = (handler >> 16) & 0xFFFF;
}

void idt_init(void)
{
    for (int v = 0; v < 32; v++)
        set_gate(v, isr_stub_table[v], GDT_KCODE, 0x8E);   /* interrupt gate */

    /* #DF e um task gate: troca para a tarefa GDT_TSS_DF e sua pilha. */
    set_gate(8, 0, GDT_TSS_DF, 0x85);

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint32_t)(uintptr_t)idt;
    __asm__ volatile ("lidt %0" : : "m"(idt_ptr));
    /* Interrupcoes de hardware seguem desligadas (cli): so excecoes da CPU. */
}

static int addr_in_stack_guard(uint32_t a)
{
    return a >= (uint32_t)(uintptr_t)stack_guard &&
           a <  (uint32_t)(uintptr_t)stack_bottom;
}

static void print_page_fault(uint32_t err, uint32_t cr2)
{
    kprintf("  endereco: 0x%08x  (%s, %s, %s%s)\n", cr2,
            (err & 1) ? "violacao de protecao" : "pagina nao presente",
            (err & 2) ? "escrita" : "leitura",
            (err & 4) ? "usuario" : "kernel",
            (err & 16) ? ", busca de instrucao" : "");

    if (cr2 < 0x1000)
        kprintf("  >> provavel desreferencia de ponteiro NULL\n");
    if (addr_in_stack_guard(cr2))
        kprintf("  >> ESTOURO DE PILHA (tocou a pagina de guarda)\n");
}

static void print_regs(const regs_t *r)
{
    kprintf("  EIP=%08x  CS=%04x  EFLAGS=%08x\n", r->eip, r->cs, r->eflags);
    kprintf("  EAX=%08x  EBX=%08x  ECX=%08x  EDX=%08x\n",
            r->eax, r->ebx, r->ecx, r->edx);
    kprintf("  ESI=%08x  EDI=%08x  EBP=%08x\n", r->esi, r->edi, r->ebp);
}

void exception_dispatch(regs_t *r)
{
    const char *name = r->vector < 32 ? exception_name[r->vector] : "?";

    /* Breakpoint e o unico recuperavel: mostra e continua (usado em testes). */
    if (r->vector == 3 && !panic_in_progress()) {
        kprintf("[#BP] breakpoint em EIP=%08x\n", r->eip);
        return;
    }

    panic_begin(name);
    kprintf("vetor %u, codigo de erro 0x%x\n", r->vector, r->error);
    if (r->vector == 14)
        print_page_fault(r->error, read_cr2());
    print_regs(r);
    panic_end();
}

/* Roda na pilha propria (df_stack) via task gate. O estado da tarefa que
 * falhou foi salvo pela CPU no TSS principal. */
void double_fault_task(void)
{
    const tss_t *t = gdt_main_tss();

    panic_begin("double fault (#DF)");
    kprintf("A CPU falhou ao entregar uma excecao.\n");
    kprintf("  EIP=%08x  ESP=%08x  EBP=%08x\n", t->eip, t->esp, t->ebp);
    kprintf("  EAX=%08x  EBX=%08x  ECX=%08x  EDX=%08x\n",
            t->eax, t->ebx, t->ecx, t->edx);
    if (t->esp < (uint32_t)(uintptr_t)stack_bottom + 256 &&
        t->esp > (uint32_t)(uintptr_t)stack_guard - 4096)
        kprintf("  >> ESTOURO DE PILHA: ESP=%08x (limite %08x)\n",
                t->esp, (uint32_t)(uintptr_t)stack_bottom);
    panic_end();
}
