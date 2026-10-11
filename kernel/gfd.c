/* OrionGFD - interface grafica do Orion OS.
 *
 * Framebuffer linear de 32 bpp entregue pelo GRUB (Multiboot), fonte 8x16,
 * janelas arrastaveis, barra de tarefas, mouse PS/2 por polling e um console
 * que substitui o terminal VGA de texto quando o GFD esta ativo.
 *
 * Sem libc, sem libgcc: nada de divisao 64 bits, memcpy/memset ou struct copy. */

#define ORION_GFD_IMPL
#include <stdint.h>
#include "gfd.h"
#include "gfd_font.h"

/* ------------------------------------------------------------------ */
/* Multiboot                                                          */
/* ------------------------------------------------------------------ */

#define MB_FLAG_FRAMEBUFFER (1u << 12)

typedef struct {
    uint32_t flags;
    uint32_t mem_lower, mem_upper, boot_device, cmdline;
    uint32_t mods_count, mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length, mmap_addr;
    uint32_t drives_length, drives_addr;
    uint32_t config_table, boot_loader_name, apm_table;
    uint32_t vbe_control_info, vbe_mode_info;
    uint16_t vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
    uint32_t fb_addr_lo, fb_addr_hi;
    uint32_t fb_pitch, fb_width, fb_height;
    uint8_t  fb_bpp, fb_type;
    uint8_t  pad[2];                    /* a uniao color_info e alinhada a 4 bytes */
    uint8_t  color_info[6];             /* r_pos, r_size, g_pos, g_size, b_pos, b_size */
} __attribute__((packed)) mb_info_t;

/* Offsets conferidos contra o GRUB (spec Multiboot 0.6.96). */
_Static_assert(__builtin_offsetof(mb_info_t, fb_addr_lo) == 88,
               "layout do multiboot_info incorreto (framebuffer)");
_Static_assert(__builtin_offsetof(mb_info_t, color_info) == 112,
               "layout do multiboot_info incorreto (color_info)");

/* ------------------------------------------------------------------ */
/* Constantes e estado                                                */
/* ------------------------------------------------------------------ */

#define FONT_W      8
#define FONT_H      16
#define TITLE_H     24
#define TOPBAR_H    28
#define TASKBAR_H   32
#define PAD         4
#define BTN_W       112
#define CON_COLS    64
#define CON_ROWS    24

enum { WIN_TERMINAL = 0, WIN_ABOUT = 1, WIN_COUNT = 2 };

typedef struct {
    int x, y, w, h;
    int visible;
    const char *title;
} gfd_window_t;

static volatile uint8_t *fb;
static int screen_w, screen_h;
static uint32_t screen_pitch;
static uint8_t shift_r, shift_g, shift_b;
static int active;

static gfd_window_t win[WIN_COUNT];
static int zorder[WIN_COUNT];           /* zorder[WIN_COUNT-1] = topo */

static int scene_dirty;
static int cursor_dirty;

/* console */
static char con_cells[CON_ROWS][CON_COLS];
static int con_row, con_col;

/* mouse */
static int mouse_ok;
static int mouse_x, mouse_y;
static int mouse_left;
static uint8_t mouse_pkt[3];
static int mouse_idx;
static int drag_win = -1, drag_dx, drag_dy;

/* ------------------------------------------------------------------ */
/* Cores                                                              */
/* ------------------------------------------------------------------ */

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << shift_r) | ((uint32_t)g << shift_g) |
           ((uint32_t)b << shift_b);
}

static uint32_t c_bg, c_panel, c_panel_dark, c_accent, c_white, c_muted,
                c_border, c_shadow, c_title_on, c_close, c_con_bg, c_con_fg,
                c_black;

static void init_palette(void)
{
    c_bg         = rgb(12, 16, 28);
    c_panel      = rgb(23, 29, 45);
    c_panel_dark = rgb(17, 22, 36);
    c_accent     = rgb(80, 145, 255);
    c_white      = rgb(235, 240, 250);
    c_muted      = rgb(125, 140, 165);
    c_border     = rgb(43, 54, 76);
    c_shadow     = rgb(5, 7, 13);
    c_title_on   = rgb(33, 46, 78);
    c_close      = rgb(230, 90, 105);
    c_con_bg     = rgb(8, 12, 22);
    c_con_fg     = rgb(130, 235, 160);
    c_black      = rgb(0, 0, 0);
}

/* ------------------------------------------------------------------ */
/* Primitivas                                                         */
/* ------------------------------------------------------------------ */

static inline volatile uint32_t *px_ptr(int x, int y)
{
    return (volatile uint32_t *)(fb + (uint32_t)y * screen_pitch +
                                 (uint32_t)x * 4u);
}

static void pixel(int x, int y, uint32_t color)
{
    if (x < 0 || y < 0 || x >= screen_w || y >= screen_h)
        return;
    *px_ptr(x, y) = color;
}

static void fill_rect(int x, int y, int w, int h, uint32_t color)
{
    int x1 = x + w, y1 = y + h;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > screen_w) x1 = screen_w;
    if (y1 > screen_h) y1 = screen_h;

    for (int yy = y; yy < y1; yy++) {
        volatile uint32_t *p = px_ptr(x, yy);
        for (int xx = x; xx < x1; xx++)
            *p++ = color;
    }
}

static void draw_char(int x, int y, char ch, uint32_t fg, uint32_t bg,
                      int opaque)
{
    if (ch < 32 || ch > 126)
        ch = ' ';

    const unsigned char *g = gfd_font[ch - 32];

    for (int r = 0; r < FONT_H; r++) {
        unsigned char bits = g[r];
        for (int c = 0; c < FONT_W; c++) {
            if (bits & (0x80u >> c))
                pixel(x + c, y + r, fg);
            else if (opaque)
                pixel(x + c, y + r, bg);
        }
    }
}

static void draw_text(int x, int y, const char *s, uint32_t fg)
{
    for (; *s; s++, x += FONT_W)
        draw_char(x, y, *s, fg, 0, 0);
}

static int text_len(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static char *append(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    return p;
}

static char *append_uint(char *p, uint32_t v)
{
    char tmp[11];
    int n = 0;

    do {
        tmp[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);

    while (n) *p++ = tmp[--n];
    return p;
}

/* ------------------------------------------------------------------ */
/* Porta de I/O                                                       */
/* ------------------------------------------------------------------ */

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t v)
{
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

/* ------------------------------------------------------------------ */
/* Mouse PS/2                                                         */
/* ------------------------------------------------------------------ */

static int ps2_wait_write(void)
{
    for (int i = 0; i < 100000; i++)
        if (!(inb(0x64) & 2)) return 1;
    return 0;
}

static int ps2_wait_read(void)
{
    for (int i = 0; i < 100000; i++)
        if (inb(0x64) & 1) return 1;
    return 0;
}

static int mouse_command(uint8_t cmd)
{
    if (!ps2_wait_write()) return 0;
    outb(0x64, 0xD4);                   /* proximo byte vai para o mouse */
    if (!ps2_wait_write()) return 0;
    outb(0x60, cmd);
    if (!ps2_wait_read()) return 0;
    return inb(0x60) == 0xFA;           /* ACK */
}

static void mouse_init(void)
{
    uint8_t cfg;

    mouse_ok = 0;

    for (int i = 0; i < 64 && (inb(0x64) & 1); i++)
        (void)inb(0x60);                /* esvazia o buffer de saida */

    if (!ps2_wait_write()) return;
    outb(0x64, 0xA8);                   /* habilita a porta auxiliar */

    if (!ps2_wait_write()) return;
    outb(0x64, 0x20);                   /* le o byte de configuracao */
    if (!ps2_wait_read()) return;
    cfg = inb(0x60);
    cfg |= 0x02;                        /* IRQ12 */
    cfg &= (uint8_t)~0x20;              /* liga o clock do mouse */

    if (!ps2_wait_write()) return;
    outb(0x64, 0x60);
    if (!ps2_wait_write()) return;
    outb(0x60, cfg);

    if (!mouse_command(0xF6)) return;   /* padroes */
    if (!mouse_command(0xF4)) return;   /* habilita o envio de dados */

    mouse_ok = 1;
}

/* Cursor com "save under": guarda os pixels que ele cobre. */
#define CUR_W 12
#define CUR_H 19

static const char *const cursor_shape[CUR_H] = {
    "X           ",
    "XX          ",
    "X.X         ",
    "X..X        ",
    "X...X       ",
    "X....X      ",
    "X.....X     ",
    "X......X    ",
    "X.......X   ",
    "X........X  ",
    "X.....XXXXX ",
    "X..X..X     ",
    "X.X X..X    ",
    "XX  X..X    ",
    "X    X..X   ",
    "     X..X   ",
    "      X..X  ",
    "      X..X  ",
    "       XX   ",
};

static uint32_t cur_saved[CUR_H][CUR_W];
static int cur_shown, cur_sx, cur_sy;

static void cursor_hide(void)
{
    if (!cur_shown) return;

    for (int r = 0; r < CUR_H; r++)
        for (int c = 0; c < CUR_W; c++)
            if (cursor_shape[r][c] != ' ')
                pixel(cur_sx + c, cur_sy + r, cur_saved[r][c]);

    cur_shown = 0;
}

static void cursor_show(void)
{
    if (!mouse_ok) return;

    cur_sx = mouse_x;
    cur_sy = mouse_y;

    for (int r = 0; r < CUR_H; r++) {
        for (int c = 0; c < CUR_W; c++) {
            char s = cursor_shape[r][c];
            int x = cur_sx + c, y = cur_sy + r;

            if (s == ' ') continue;

            cur_saved[r][c] = (x >= 0 && y >= 0 && x < screen_w &&
                               y < screen_h) ? *px_ptr(x, y) : 0;
            pixel(x, y, s == 'X' ? c_black : c_white);
        }
    }

    cur_shown = 1;
}

/* ------------------------------------------------------------------ */
/* Janelas                                                            */
/* ------------------------------------------------------------------ */

static int top_visible(void)
{
    for (int i = WIN_COUNT - 1; i >= 0; i--)
        if (win[zorder[i]].visible)
            return zorder[i];
    return -1;
}

static void raise_window(int idx)
{
    int pos = 0;

    while (zorder[pos] != idx) pos++;
    for (; pos < WIN_COUNT - 1; pos++)
        zorder[pos] = zorder[pos + 1];
    zorder[WIN_COUNT - 1] = idx;
}

static int window_at(int x, int y)
{
    for (int i = WIN_COUNT - 1; i >= 0; i--) {
        gfd_window_t *w = &win[zorder[i]];

        if (w->visible && x >= w->x && x < w->x + w->w &&
            y >= w->y && y < w->y + w->h)
            return zorder[i];
    }
    return -1;
}

static void clamp_window(gfd_window_t *w)
{
    int max_x = screen_w - w->w;
    int max_y = screen_h - TASKBAR_H - TITLE_H;

    if (w->x > max_x) w->x = max_x;
    if (w->x < 0)     w->x = 0;
    if (w->y > max_y) w->y = max_y;
    if (w->y < TOPBAR_H) w->y = TOPBAR_H;
}

static void draw_console(const gfd_window_t *w)
{
    int ox = w->x + PAD;
    int oy = w->y + TITLE_H + PAD;

    fill_rect(w->x, w->y + TITLE_H, w->w, w->h - TITLE_H, c_con_bg);

    for (int r = 0; r < CON_ROWS; r++)
        for (int c = 0; c < CON_COLS; c++) {
            char ch = con_cells[r][c];
            if (ch != ' ')
                draw_char(ox + c * FONT_W, oy + r * FONT_H, ch,
                          c_con_fg, c_con_bg, 0);
        }

    /* cursor de texto: bloco com o caractere invertido */
    fill_rect(ox + con_col * FONT_W, oy + con_row * FONT_H,
              FONT_W, FONT_H, c_con_fg);
    draw_char(ox + con_col * FONT_W, oy + con_row * FONT_H,
              con_cells[con_row][con_col], c_con_bg, c_con_fg, 0);
}

static void draw_about(const gfd_window_t *w)
{
    char line[48];
    char *p;
    int x = w->x + 14, y = w->y + TITLE_H + 12;

    draw_text(x, y,        "OrionGFD 0.1.0", c_accent);
    draw_text(x, y + 20,   "Interface grafica do Orion OS", c_white);

    p = append(line, "Tela: ");
    p = append_uint(p, (uint32_t)screen_w);
    p = append(p, "x");
    p = append_uint(p, (uint32_t)screen_h);
    p = append(p, " (32 bpp)");
    *p = '\0';
    draw_text(x, y + 52, line, c_muted);

    draw_text(x, y + 72, mouse_ok ? "Mouse: ativo" : "Mouse: nao detectado",
              c_muted);
    draw_text(x, y + 100, "Arraste a barra de titulo para", c_muted);
    draw_text(x, y + 118, "mover. A barra inferior abre e", c_muted);
    draw_text(x, y + 136, "oculta as janelas.", c_muted);
}

static void draw_window(int idx, int focused)
{
    const gfd_window_t *w = &win[idx];

    fill_rect(w->x + 4, w->y + 4, w->w, w->h, c_shadow);
    fill_rect(w->x - 1, w->y - 1, w->w + 2, w->h + 2, c_border);
    fill_rect(w->x, w->y, w->w, w->h, c_panel);

    fill_rect(w->x, w->y, w->w, TITLE_H, focused ? c_title_on : c_panel_dark);
    fill_rect(w->x, w->y, 3, TITLE_H, focused ? c_accent : c_muted);
    draw_text(w->x + 12, w->y + 4, w->title, c_white);

    fill_rect(w->x + w->w - 22, w->y + 4, 16, 16, c_close);
    draw_char(w->x + w->w - 18, w->y + 4, 'x', c_white, 0, 0);

    if (idx == WIN_TERMINAL)
        draw_console(w);
    else
        draw_about(w);
}

static void draw_scene(void)
{
    int top = top_visible();

    fill_rect(0, 0, screen_w, screen_h, c_bg);

    /* barra superior */
    fill_rect(0, 0, screen_w, TOPBAR_H, c_panel_dark);
    fill_rect(0, TOPBAR_H - 1, screen_w, 1, c_border);
    fill_rect(12, 6, 4, 16, c_accent);
    fill_rect(19, 6, 4, 16, c_white);
    fill_rect(26, 6, 4, 16, c_accent);
    draw_text(42, 6, "OrionGFD", c_white);
    draw_text(screen_w - 16 - 14 * FONT_W, 6, "Orion OS 0.4.0", c_muted);

    for (int i = 0; i < WIN_COUNT; i++) {
        int idx = zorder[i];
        if (win[idx].visible)
            draw_window(idx, idx == top);
    }

    /* barra de tarefas */
    int ty = screen_h - TASKBAR_H;
    fill_rect(0, ty, screen_w, TASKBAR_H, c_panel_dark);
    fill_rect(0, ty, screen_w, 1, c_border);

    for (int i = 0; i < WIN_COUNT; i++) {
        int bx = 8 + i * (BTN_W + 6);
        uint32_t bg = !win[i].visible ? c_panel_dark :
                      (i == top ? c_title_on : c_panel);

        fill_rect(bx - 1, ty + 3, BTN_W + 2, 26, c_border);
        fill_rect(bx, ty + 4, BTN_W, 24, bg);
        if (win[i].visible)
            fill_rect(bx, ty + 26, BTN_W, 2, i == top ? c_accent : c_muted);
        draw_text(bx + (BTN_W - text_len(win[i].title) * FONT_W) / 2, ty + 8,
                  win[i].title, win[i].visible ? c_white : c_muted);
    }
}

/* ------------------------------------------------------------------ */
/* Console                                                            */
/* ------------------------------------------------------------------ */

static void con_clear_cells(void)
{
    for (int r = 0; r < CON_ROWS; r++)
        for (int c = 0; c < CON_COLS; c++)
            con_cells[r][c] = ' ';
    con_row = 0;
    con_col = 0;
}

static void con_scroll(void)
{
    for (int r = 1; r < CON_ROWS; r++)
        for (int c = 0; c < CON_COLS; c++)
            con_cells[r - 1][c] = con_cells[r][c];

    for (int c = 0; c < CON_COLS; c++)
        con_cells[CON_ROWS - 1][c] = ' ';

    con_row = CON_ROWS - 1;
}

int gfd_console_putchar(char c)
{
    if (!active)
        return 0;

    if (c == '\n') {
        con_col = 0;
        con_row++;
    } else if (c == '\r') {
        con_col = 0;
    } else if (c == '\b') {
        if (con_col > 0) {
            con_col--;
            con_cells[con_row][con_col] = ' ';
        }
    } else if (c == '\t') {
        int spaces = 4 - (con_col % 4);
        while (spaces--)
            gfd_console_putchar(' ');
        return 1;
    } else {
        con_cells[con_row][con_col] = c;
        con_col++;
        if (con_col >= CON_COLS) {
            con_col = 0;
            con_row++;
        }
    }

    if (con_row >= CON_ROWS)
        con_scroll();

    scene_dirty = 1;
    return 1;
}

int gfd_console_clear(void)
{
    if (!active)
        return 0;

    con_clear_cells();
    scene_dirty = 1;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Entrada do mouse                                                   */
/* ------------------------------------------------------------------ */

static void taskbar_click(int mx)
{
    for (int i = 0; i < WIN_COUNT; i++) {
        int bx = 8 + i * (BTN_W + 6);

        if (mx < bx || mx >= bx + BTN_W)
            continue;

        if (!win[i].visible) {
            win[i].visible = 1;
            raise_window(i);
        } else if (top_visible() == i) {
            win[i].visible = 0;
        } else {
            raise_window(i);
        }
        scene_dirty = 1;
        return;
    }
}

static void left_pressed(void)
{
    if (mouse_y >= screen_h - TASKBAR_H) {
        taskbar_click(mouse_x);
        return;
    }

    int idx = window_at(mouse_x, mouse_y);
    if (idx < 0)
        return;

    gfd_window_t *w = &win[idx];
    int on_close = mouse_x >= w->x + w->w - 22 && mouse_x < w->x + w->w - 6 &&
                   mouse_y >= w->y + 4 && mouse_y < w->y + 20;

    raise_window(idx);
    scene_dirty = 1;

    if (on_close) {
        w->visible = 0;
    } else if (mouse_y < w->y + TITLE_H) {
        drag_win = idx;
        drag_dx = mouse_x - w->x;
        drag_dy = mouse_y - w->y;
    }
}

static void mouse_packet(void)
{
    uint8_t flags = mouse_pkt[0];
    int dx, dy, left;

    if (flags & 0xC0)                   /* overflow: descarta */
        return;

    dx = (int)mouse_pkt[1] - ((flags & 0x10) ? 256 : 0);
    dy = (int)mouse_pkt[2] - ((flags & 0x20) ? 256 : 0);

    mouse_x += dx;
    mouse_y -= dy;                      /* o eixo Y do PS/2 e invertido */
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= screen_w) mouse_x = screen_w - 1;
    if (mouse_y >= screen_h) mouse_y = screen_h - 1;
    cursor_dirty = 1;

    left = flags & 1;

    if (left && !mouse_left) {
        left_pressed();
    } else if (left && drag_win >= 0) {
        gfd_window_t *w = &win[drag_win];
        w->x = mouse_x - drag_dx;
        w->y = mouse_y - drag_dy;
        clamp_window(w);
        scene_dirty = 1;
    } else if (!left) {
        drag_win = -1;
    }

    mouse_left = left;
}

int gfd_mouse_feed(unsigned char byte)
{
    if (!active || !mouse_ok)
        return 0;

    /* O bit 3 do primeiro byte e sempre 1: serve para ressincronizar. */
    if (mouse_idx == 0 && !(byte & 0x08))
        return 1;

    mouse_pkt[mouse_idx++] = byte;

    if (mouse_idx == 3) {
        mouse_idx = 0;
        mouse_packet();
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* API                                                                */
/* ------------------------------------------------------------------ */

int gfd_active(void)
{
    return active;
}

void gfd_poll(void)
{
    if (!active)
        return;

    if (scene_dirty) {
        cursor_hide();
        draw_scene();
        cursor_show();
    } else if (cursor_dirty) {
        cursor_hide();
        cursor_show();
    }

    scene_dirty = 0;
    cursor_dirty = 0;
}

int gfd_init(unsigned int multiboot_address)
{
    const mb_info_t *info = (const mb_info_t *)(uintptr_t)multiboot_address;

    active = 0;

    if (!info || !(info->flags & MB_FLAG_FRAMEBUFFER))
        return 0;

    if (info->fb_addr_hi != 0 || info->fb_addr_lo == 0)
        return 0;

    if (info->fb_bpp != 32 || info->fb_type != 1)
        return 0;

    if (info->fb_width < 640 || info->fb_width > 4096 ||
        info->fb_height < 480 || info->fb_height > 2160 ||
        info->fb_pitch < info->fb_width * 4u)
        return 0;

    shift_r = info->color_info[0];
    shift_g = info->color_info[2];
    shift_b = info->color_info[4];
    if (shift_r > 24 || shift_g > 24 || shift_b > 24)
        return 0;

    fb = (volatile uint8_t *)(uintptr_t)info->fb_addr_lo;
    screen_w = (int)info->fb_width;
    screen_h = (int)info->fb_height;
    screen_pitch = info->fb_pitch;

    init_palette();
    con_clear_cells();

    /* Janela Terminal: area de texto CON_COLS x CON_ROWS. */
    win[WIN_TERMINAL].title   = "Terminal";
    win[WIN_TERMINAL].w       = CON_COLS * FONT_W + 2 * PAD;
    win[WIN_TERMINAL].h       = TITLE_H + CON_ROWS * FONT_H + 2 * PAD;
    win[WIN_TERMINAL].x       = 16;
    win[WIN_TERMINAL].y       = TOPBAR_H + 14;
    win[WIN_TERMINAL].visible = 1;

    win[WIN_ABOUT].title   = "Sobre";
    win[WIN_ABOUT].w       = 320;
    win[WIN_ABOUT].h       = TITLE_H + 170;
    win[WIN_ABOUT].x       = screen_w - 320 - 12;
    win[WIN_ABOUT].y       = TOPBAR_H + 40;
    win[WIN_ABOUT].visible = 1;

    for (int i = 0; i < WIN_COUNT; i++)
        clamp_window(&win[i]);

    zorder[0] = WIN_ABOUT;              /* o terminal comeca em foco */
    zorder[1] = WIN_TERMINAL;

    mouse_x = screen_w / 2;
    mouse_y = screen_h / 2;
    mouse_init();

    active = 1;
    scene_dirty = 1;
    gfd_poll();
    return 1;
}
