
#include <stdint.h>

#define MULTIBOOT_FRAMEBUFFER_INFO  (1u << 12)

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint8_t reserved[44];

    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint8_t color_info[6];
} __attribute__((packed)) multiboot_info_t;

static volatile uint32_t *framebuffer;
static uint32_t screen_width;
static uint32_t screen_height;
static uint32_t screen_pitch;
static uint8_t red_position;
static uint8_t green_position;
static uint8_t blue_position;

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << red_position) |
           ((uint32_t)g << green_position) |
           ((uint32_t)b << blue_position);
}

static void pixel(int x, int y, uint32_t color)
{
    if (!framebuffer || x < 0 || y < 0 ||
        (uint32_t)x >= screen_width ||
        (uint32_t)y >= screen_height)
        return;

    volatile uint8_t *base = (volatile uint8_t *)framebuffer;
    volatile uint32_t *p =
        (volatile uint32_t *)(base + (uint32_t)y * screen_pitch
                                   + (uint32_t)x * 4u);
    *p = color;
}

static void fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0)
        return;

    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            pixel(xx, yy, color);
}

static void draw_desktop(void)
{
    uint32_t background = rgb(12, 16, 28);
    uint32_t panel      = rgb(23, 29, 45);
    uint32_t panel_dark = rgb(17, 22, 36);
    uint32_t accent     = rgb(80, 145, 255);
    uint32_t white      = rgb(235, 240, 250);
    uint32_t muted      = rgb(125, 140, 165);
    uint32_t border     = rgb(43, 54, 76);

    fill_rect(0, 0, screen_width, screen_height, background);

    /* Barra superior */
    fill_rect(0, 0, screen_width, 38, panel_dark);
    fill_rect(0, 37, screen_width, 1, border);

    /* Marca geométrica do Orion */
    fill_rect(16, 11, 4, 16, accent);
    fill_rect(23, 11, 4, 16, white);
    fill_rect(30, 11, 4, 16, accent);

    /* Indicadores à direita */
    fill_rect((int)screen_width - 100, 15, 7, 7, rgb(75, 210, 145));
    fill_rect((int)screen_width - 83, 15, 7, 7, muted);
    fill_rect((int)screen_width - 66, 15, 7, 7, muted);

    /* Painel lateral */
    fill_rect(0, 38, 170, (int)screen_height - 38, panel_dark);
    fill_rect(169, 38, 1, (int)screen_height - 38, border);

    /* Itens do painel lateral */
    fill_rect(14, 60, 140, 34, panel);
    fill_rect(22, 71, 12, 12, accent);
    fill_rect(43, 72, 80, 3, white);
    fill_rect(43, 79, 60, 3, muted);

    fill_rect(22, 112, 12, 12, muted);
    fill_rect(43, 115, 70, 3, muted);

    fill_rect(22, 146, 12, 12, muted);
    fill_rect(43, 149, 85, 3, muted);

    fill_rect(22, 180, 12, 12, muted);
    fill_rect(43, 183, 65, 3, muted);

    /* Janela principal */
    int margin_x = 205;
    int margin_y = 82;
    int win_w = (int)screen_width - margin_x - 35;
    int win_h = (int)screen_height - margin_y - 75;

    if (win_w > 120 && win_h > 100) {
        fill_rect(margin_x, margin_y, win_w, win_h, panel);
        fill_rect(margin_x, margin_y, win_w, 36, panel_dark);
        fill_rect(margin_x, margin_y, win_w, 1, border);
        fill_rect(margin_x, margin_y + 35, win_w, 1, border);

        /* Controles da janela */
        fill_rect(margin_x + win_w - 55, margin_y + 14, 8, 8,
                  rgb(235, 190, 80));
        fill_rect(margin_x + win_w - 38, margin_y + 14, 8, 8,
                  rgb(80, 190, 135));
        fill_rect(margin_x + win_w - 21, margin_y + 14, 8, 8,
                  rgb(230, 90, 105));

        /* Cartões */
        int card_y = margin_y + 58;
        int card_w = (win_w - 48) / 3;

        for (int i = 0; i < 3; i++) {
            int card_x = margin_x + 16 + i * (card_w + 8);
            fill_rect(card_x, card_y, card_w, 90, panel_dark);
            fill_rect(card_x, card_y, 3, 90, accent);
            fill_rect(card_x + 13, card_y + 16, card_w - 26, 4, white);
            fill_rect(card_x + 13, card_y + 29, card_w - 42, 3, muted);
            fill_rect(card_x + 13, card_y + 42, card_w - 34, 3, muted);
        }

        /* Área de conteúdo */
        fill_rect(margin_x + 16, card_y + 112, win_w - 32, 1, border);
        fill_rect(margin_x + 16, card_y + 130, win_w / 2, 4, muted);
        fill_rect(margin_x + 16, card_y + 143, win_w - 64, 4, border);
        fill_rect(margin_x + 16, card_y + 156, win_w - 90, 4, border);
    }

    /* Barra inferior */
    fill_rect(0, (int)screen_height - 36, screen_width, 36, panel_dark);
    fill_rect(0, (int)screen_height - 37, screen_width, 1, border);

    /* Ícone central e indicadores */
    int task_x = (int)screen_width / 2 - 48;
    fill_rect(task_x, (int)screen_height - 27, 18, 18, accent);
    fill_rect(task_x + 26, (int)screen_height - 23, 10, 10, muted);
    fill_rect(task_x + 44, (int)screen_height - 23, 10, 10, muted);

    /* Relógio ilustrativo */
    fill_rect((int)screen_width - 54, (int)screen_height - 23, 35, 3, white);
    fill_rect((int)screen_width - 54, (int)screen_height - 17, 25, 2, muted);
}

void orion_gui_init(uint32_t multiboot_address)
{
    if (!multiboot_address)
        return;

    const multiboot_info_t *info =
        (const multiboot_info_t *)(uintptr_t)multiboot_address;

    if (!(info->flags & MULTIBOOT_FRAMEBUFFER_INFO))
        return;

    if (info->framebuffer_addr >> 32)
        return;

    if (info->framebuffer_bpp != 32 || info->framebuffer_type != 1)
        return;

    if (info->framebuffer_width < 320 ||
        info->framebuffer_width > 4096 ||
        info->framebuffer_height < 200 ||
        info->framebuffer_height > 2160 ||
        info->framebuffer_pitch < info->framebuffer_width * 4u)
        return;

    red_position = info->color_info[0];
    green_position = info->color_info[2];
    blue_position = info->color_info[4];

    if (red_position > 24 || green_position > 24 || blue_position > 24)
        return;

    framebuffer = (volatile uint32_t *)(uintptr_t)info->framebuffer_addr;
    screen_width = info->framebuffer_width;
    screen_height = info->framebuffer_height;
    screen_pitch = info->framebuffer_pitch;

    draw_desktop();
}
