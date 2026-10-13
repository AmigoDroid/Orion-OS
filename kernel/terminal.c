
#include "terminal.h"
#include "../gfx/gfd.h"
#include "log.h"
#include "io.h"

typedef unsigned char  u8;
typedef unsigned short u16;

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

static volatile u16 *const VGA_MEMORY = (volatile u16 *)0xB8000;

static unsigned int terminal_row;
static unsigned int terminal_column;
static u8 terminal_color;

void terminal_update_cursor(void)
{
    unsigned short position =
        terminal_row * VGA_WIDTH + terminal_column;

    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(position & 0xFF));

    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char)(position >> 8));
}


static u16 make_vga_entry(char c, u8 color)
{
    return (u8)c | ((u16)color << 8);
}

void terminal_clear(void)
{
    if (gfd_console_clear())
        return;

    u16 blank = make_vga_entry(' ', terminal_color);

    for (unsigned int y = 0; y < VGA_HEIGHT; y++) {
        for (unsigned int x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[y * VGA_WIDTH + x] = blank;
        }
    }

    terminal_row = 0;
    terminal_column = 0;
}

static void terminal_scroll(void)
{
    for (unsigned int y = 1; y < VGA_HEIGHT; y++) {
        for (unsigned int x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] =
                VGA_MEMORY[y * VGA_WIDTH + x];
        }
    }

    u16 blank = make_vga_entry(' ', terminal_color);

    for (unsigned int x = 0; x < VGA_WIDTH; x++) {
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }

    terminal_row = VGA_HEIGHT - 1;
}


void terminal_putchar(char c)
{
    log_putc(c);                    /* espelho em 0xE9: transcript do boot */

    if (gfd_console_putchar(c))
        return;

    if (c == '\n') {
        terminal_column = 0;
        terminal_row++;
    } else if (c == '\r') {
        terminal_column = 0;
    } else if (c == '\b') {
        if (terminal_column > 0) {
            terminal_column--;
            VGA_MEMORY[terminal_row * VGA_WIDTH + terminal_column] =
                make_vga_entry(' ', terminal_color);
        }
    } else if (c == '\t') {
        unsigned int spaces = 4 - (terminal_column % 4);

        for (unsigned int i = 0; i < spaces; i++)
            terminal_putchar(' ');

        return;
    } else {
        VGA_MEMORY[terminal_row * VGA_WIDTH + terminal_column] =
            make_vga_entry(c, terminal_color);

        terminal_column++;

        if (terminal_column >= VGA_WIDTH) {
            terminal_column = 0;
            terminal_row++;
        }
    }

    if (terminal_row >= VGA_HEIGHT) {
        terminal_scroll();
    }

    terminal_update_cursor();
}


void terminal_writestring(const char *str)
{
    while (*str != '\0') {
        terminal_putchar(*str);
        str++;
    }
}

void terminal_setcolor(unsigned char color)
{
    terminal_color = color;
}

void terminal_initialize(void)
{
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = 0x0A;

    terminal_clear();
}
