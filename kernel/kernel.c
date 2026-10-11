#include "terminal.h"
#include "keyboard.h"
#include "ata.h"
#include "fs.h"
#include "gfd.h"

#define PROMPT " orion> "
#define INPUT_SIZE 256

static char input[INPUT_SIZE];

static void execute_command(char *line);

/* Fica no inicio do .text (ver linker.ld): o stage2 salta para 0x10000. */
__attribute__((section(".text.start")))
void kernel_main(void)
{
    unsigned int input_length = 0;
    int c;

    terminal_initialize();
    keyboard_initialize();

   terminal_writestring("       ___  ____  _              \n");
    terminal_writestring("      / _ \\|  _ \\(_) ___  _ __   \n");
    terminal_writestring("     | | | | |_) | |/ _ \\| '_ \\  \n");
    terminal_writestring("     | |_| |  _ <| | (_) | | | | \n");
    terminal_writestring("      \\___/|_| \\_\\_|\\___/|_| |_| \n");
    terminal_writestring("                                 \n");
    terminal_writestring("       -- ORION OS 0.3.0 --      \n");
    terminal_writestring("---------------------------------\n\n");
    terminal_writestring(" Kernel iniciado com sucesso.\n");
    terminal_writestring(" Teclado inicializado.\n");
    terminal_writestring(" Inicializando OrionFS...\n\n");

    fs_mount();

    terminal_writestring("Digite 'help' para ver os comandos.\n\n");
    terminal_writestring(PROMPT);

    /* Sem IDT e com interrupcoes desabilitadas (cli no stage2),
       o teclado e lido por polling. Nao use hlt aqui. */
    for (;;) {
        if (gfd_poll)
            gfd_poll();

        c = keyboard_getchar();

        if (c < 0)
            continue;

        if (c == '\n') {
            terminal_putchar('\n');
            input[input_length] = '\0';
            execute_command(input);
            input_length = 0;
            terminal_writestring(PROMPT);
        } else if (c == '\b') {
            if (input_length > 0) {
                input_length--;
                terminal_putchar('\b');
            }
        } else if (input_length < INPUT_SIZE - 1) {
            input[input_length++] = (char)c;
            terminal_putchar((char)c);
        }
    }
}

static int string_equal(const char *a, const char *b)
{
    int i = 0;

    while (a[i] && b[i]) {
        if (a[i] != b[i])
            return 0;
        i++;
    }

    return a[i] == b[i];
}

static int string_starts_with(const char *text, const char *prefix)
{
    int i = 0;

    while (prefix[i]) {
        if (text[i] != prefix[i])
            return 0;
        i++;
    }

    return 1;
}

static char *skip_spaces(char *text)
{
    while (*text == ' ')
        text++;

    return text;
}

static void print_help(void)
{
    terminal_writestring("\nComandos do Orion OS:\n\n");
    terminal_writestring("  help                  Mostra esta ajuda\n");
    terminal_writestring("  clear                 Limpa a tela\n");
    terminal_writestring("  ls                    Lista os arquivos\n");
    terminal_writestring("  format                Formata o OrionFS\n");
    terminal_writestring("  create <nome>         Cria um arquivo\n");
    terminal_writestring("  write <nome> <texto>  Grava texto no arquivo\n");
    terminal_writestring("  read <nome>           Le um arquivo\n");
    terminal_writestring("  rm <nome>             Remove um arquivo\n");
    terminal_writestring("\n");
}

static void execute_command(char *line)
{
    char *command = skip_spaces(line);
    char *argument;
    char *content;

    if (*command == '\0')
        return;

    if (string_equal(command, "help")) {
        print_help();
        return;
    }

    if (string_equal(command, "clear")) {
        terminal_initialize();
        return;
    }

    if (string_equal(command, "ls")) {
        fs_list();
        return;
    }

    if (string_equal(command, "format")) {
        terminal_writestring("Formatando o OrionFS...\n");

        if (fs_format()) {
            terminal_writestring(
                "Erro ao formatar. Verifique o disco ATA.\n"
            );
        } else {
            terminal_writestring("OrionFS formatado.\n");
            fs_mount();
        }

        return;
    }

    if (string_starts_with(command, "create ")) {
        argument = skip_spaces(command + 7);

        if (*argument == '\0') {
            terminal_writestring("Uso: create <nome>\n");
            return;
        }

        if (fs_create(argument, 0)) {
            terminal_writestring(" Nao foi possivel criar o arquivo.\n");
        } else {
            terminal_writestring(" Arquivo criado.\n");
        }

        return;
    }

    if (string_starts_with(command, "write ")) {
        argument = skip_spaces(command + 6);
        content = argument;

        while (*content && *content != ' ')
            content++;

        if (*content == '\0') {
            terminal_writestring("Uso: write <nome> <texto>\n");
            return;
        }

        *content = '\0';
        content = skip_spaces(content + 1);

        if (*argument == '\0' || *content == '\0') {
            terminal_writestring("Uso: write <nome> <texto>\n");
            return;
        }

        if (fs_write(argument, content)) {
            terminal_writestring(" Erro ao gravar o arquivo.\n");
        } else {
            terminal_writestring(" Arquivo gravado.\n");
        }

        return;
    }

    if (string_starts_with(command, "read ")) {
        argument = skip_spaces(command + 5);

        if (*argument == '\0') {
            terminal_writestring("Uso: read <nome>\n");
            return;
        }

        fs_read(argument);
        return;
    }

    if (string_starts_with(command, "rm ")) {
        argument = skip_spaces(command + 3);

        if (*argument == '\0') {
            terminal_writestring(" Uso: rm <nome>\n");
            return;
        }

        if (fs_remove(argument)) {
            terminal_writestring(" Nao foi possivel remover o arquivo.\n");
        } else {
            terminal_writestring(" Arquivo removido.\n");
        }

        return;
    }

    terminal_writestring(" Comando desconhecido: ");
    terminal_writestring(command);
    terminal_writestring("\nDigite 'help' para ver os comandos.\n");
}
