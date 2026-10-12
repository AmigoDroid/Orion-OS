/* Shell do Orion: le o teclado (polling), interpreta e executa comandos.
 * Cada comando e uma linha da tabela `commands`; para criar um novo,
 * escreva a funcao cmd_xxx e acrescente uma linha. */

#include "shell.h"
#include "kprintf.h"
#include "terminal.h"
#include "string.h"
#include "version.h"
#include "io.h"
#include "panic.h"
#include "mm.h"
#include "fs.h"
#include "gfd.h"
#include "pci.h"
#include "usb.h"
#include "keyboard.h"
#include "mouse.h"

#define PROMPT     " orion> "
#define INPUT_SIZE 256

static const mb_info_t *boot_info;

static char *skip_spaces(char *s)
{
    while (*s == ' ')
        s++;
    return s;
}

/* ---------------------------- comandos basicos ---------------------------- */

static void cmd_help(char *args);

static void cmd_clear(char *args)   { (void)args; terminal_initialize(); }
static void cmd_pci(char *args)     { (void)args; pci_list_devices(); }
static void cmd_usb(char *args)     { (void)args; usb_list_controllers(); }

static void cmd_info(char *args)
{
    (void)args;
    kprintf("Orion OS %s (i386, Multiboot)\n", ORION_VERSION);
    if (boot_info->flags & MB_FLAG_LOADER_NAME)
        kprintf("  bootloader: %s\n", (const char *)(uintptr_t)boot_info->boot_loader_name);
    kprintf("  video: %s\n", gfd_active() ? "OrionGFD (framebuffer)" : "texto VGA 80x25");
    kprintf("  paginacao: %s\n", vmm_enabled() ? "ligada" : "desligada");
}

static void cmd_reboot(char *args)
{
    (void)args;
    kprintf("Reiniciando...\n");
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) { }
    outb(0x64, 0xFE);                       /* pulso de reset do controlador PS/2 */
    cpu_halt_forever();
}

static void cmd_halt(char *args)
{
    (void)args;
    kprintf("Sistema parado.\n");
    gfd_poll();
    cpu_halt_forever();
}

/* ---------------------------- arquivos (OrionFS) ---------------------------- */

static void cmd_ls(char *args)      { (void)args; fs_list(); }

static void cmd_format(char *args)
{
    (void)args;
    kprintf("Formatando o OrionFS...\n");
    if (fs_format()) {
        kprintf("Erro ao formatar. Verifique o disco ATA.\n");
    } else {
        kprintf("OrionFS formatado.\n");
        fs_mount();
    }
}

static void cmd_create(char *args)
{
    if (*args == '\0') { kprintf("Uso: create <nome>\n"); return; }
    kprintf(fs_create(args, 0) ? " Nao foi possivel criar o arquivo.\n"
                               : " Arquivo criado.\n");
}

static void cmd_write(char *args)
{
    char *text = args;

    while (*text && *text != ' ')
        text++;
    if (*text == '\0') { kprintf("Uso: write <nome> <texto>\n"); return; }
    *text = '\0';
    text = skip_spaces(text + 1);
    if (*args == '\0' || *text == '\0') { kprintf("Uso: write <nome> <texto>\n"); return; }

    kprintf(fs_write(args, text) ? " Erro ao gravar o arquivo.\n"
                                 : " Arquivo gravado.\n");
}

static void cmd_read(char *args)
{
    if (*args == '\0') { kprintf("Uso: read <nome>\n"); return; }
    fs_read(args);
}

static void cmd_rm(char *args)
{
    if (*args == '\0') { kprintf(" Uso: rm <nome>\n"); return; }
    kprintf(fs_remove(args) ? " Nao foi possivel remover o arquivo.\n"
                            : " Arquivo removido.\n");
}

/* ---------------------------- memoria ---------------------------- */

static void cmd_mem(char *args)     { (void)args; mm_print_info(); }
static void cmd_memmap(char *args)  { (void)args; mm_print_map(); }
static void cmd_vmm(char *args)     { (void)args; vmm_print_info(); }
static void cmd_memtest(char *args) { (void)args; mm_selftest(); }

static void cmd_heapcheck(char *args)
{
    int deep = strcmp(args, "deep") == 0;
    int errors = heap_check(deep, 1);

    errors += pmm_check();
    if (errors)
        kprintf("Heap/PMM: %d problema(s) encontrado(s).\n", errors);
    else
        kprintf("Heap/PMM consistentes%s.\n", deep ? " (verificacao profunda)" : "");
}

/* ---------------------------- provocar falhas (teste) ---------------------------- */

static int __attribute__((noinline)) recurse(int n)
{
    volatile char pad[512];

    pad[0] = (char)n;
    __asm__ volatile ("" ::: "memory");
    return recurse(n + 1) + pad[0];
}

static void cmd_crash(char *args)
{
    if (strcmp(args, "null") == 0) {
        *(volatile int *)0 = 1;
    } else if (strcmp(args, "read") == 0) {
        volatile int x = *(volatile int *)0;
        (void)x;
    } else if (strcmp(args, "text") == 0) {
        *(volatile uint8_t *)(uintptr_t)shell_run = 0;      /* escreve no codigo */
    } else if (strcmp(args, "stack") == 0) {
        recurse(0);
    } else if (strcmp(args, "div") == 0) {
        /* em C o GCC apagaria 1/0 (comportamento indefinido): usa a instrucao */
        __asm__ volatile ("xor %%edx, %%edx\n\tdiv %%ecx" : : "a"(1), "c"(0) : "edx");
    } else if (strcmp(args, "ud") == 0) {
        __asm__ volatile ("ud2");
    } else if (strcmp(args, "bp") == 0) {
        __asm__ volatile ("int3");                          /* recuperavel */
    } else if (strcmp(args, "heap") == 0) {
        char *p = kmalloc(16);
        p[16] = 0;                                          /* 1 byte alem */
        kfree(p);
    } else if (strcmp(args, "free2") == 0) {
        void *p = kmalloc(16);
        kfree(p);
        kfree(p);
    } else if (strcmp(args, "panic") == 0) {
        panic("panic de teste pedido pelo usuario");
    } else {
        kprintf("Uso: crash <null|read|text|stack|div|ud|bp|heap|free2|panic>\n"
                "Provoca de proposito uma falha para testar o tratamento de erros.\n");
    }
}

/* ---------------------------- tabela ---------------------------- */

typedef struct {
    const char *name;
    const char *usage;
    const char *help;
    void (*run)(char *args);
} command_t;

static const command_t commands[] = {
    { "help",      "",                "Mostra esta ajuda",           cmd_help },
    { "info",      "",                "Versao, bootloader e video",  cmd_info },
    { "clear",     "",                "Limpa a tela",                cmd_clear },
    { "pci",       "",                "Lista dispositivos PCI",      cmd_pci },
    { "usb",       "",                "Controladores USB",           cmd_usb },
    { "ls",        "",                "Lista os arquivos",           cmd_ls },
    { "format",    "",                "Formata o OrionFS",           cmd_format },
    { "create",    "<nome>",          "Cria um arquivo",             cmd_create },
    { "write",     "<nome> <texto>",  "Grava texto no arquivo",      cmd_write },
    { "read",      "<nome>",          "Le um arquivo",               cmd_read },
    { "rm",        "<nome>",          "Remove um arquivo",           cmd_rm },
    { "mem",       "",                "Uso de memoria (PMM e heap)", cmd_mem },
    { "memmap",    "",                "Mapa de memoria do firmware", cmd_memmap },
    { "vmm",       "",                "Estado da paginacao",         cmd_vmm },
    { "heapcheck", "[deep]",          "Verifica a integridade",      cmd_heapcheck },
    { "memtest",   "",                "Autoteste do gerenciador",    cmd_memtest },
    { "crash",     "<tipo>",          "Provoca uma falha (teste)",   cmd_crash },
    { "reboot",    "",                "Reinicia a maquina",          cmd_reboot },
    { "halt",      "",                "Para a CPU",                  cmd_halt },
};

#define NCOMMANDS ((int)(sizeof(commands) / sizeof(commands[0])))

static void cmd_help(char *args)
{
    (void)args;
    kprintf("\nComandos do Orion OS:\n\n");
    for (int i = 0; i < NCOMMANDS; i++)
        kprintf("  %-9s %-15s %s\n", commands[i].name, commands[i].usage, commands[i].help);
    kprintf("\n");
}

static void execute(char *line)
{
    char *name = skip_spaces(line), *args = name;

    if (*name == '\0')
        return;

    while (*args && *args != ' ')
        args++;
    if (*args) {
        *args++ = '\0';
        args = skip_spaces(args);
    }

    for (int i = 0; i < NCOMMANDS; i++) {
        if (strcmp(name, commands[i].name) == 0) {
            commands[i].run(args);
            return;
        }
    }
    kprintf(" Comando desconhecido: %s\nDigite 'help' para ver os comandos.\n", name);
}

void shell_run(const mb_info_t *mbi)
{
    static char input[INPUT_SIZE];
    unsigned int length = 0;

    boot_info = mbi;
    kprintf("Digite 'help' para ver os comandos.\n\n" PROMPT);

    /* Sem IRQs (cli) o teclado e lido por polling; nao use hlt aqui. */
    for (;;) {
        int c;

        gfd_poll();
        mouse_poll();
        c = keyboard_getchar();
        if (c < 0)
            continue;

        if (c == '\n') {
            terminal_putchar('\n');
            input[length] = '\0';
            execute(input);
            length = 0;
            kprintf(PROMPT);
        } else if (c == '\b') {
            if (length > 0) {
                length--;
                terminal_putchar('\b');
            }
        } else if (length < INPUT_SIZE - 1) {
            input[length++] = (char)c;
            terminal_putchar((char)c);
        }
    }
}
