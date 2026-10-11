#include "graphics.h"

#define GFX_MAX_WIDTH  1920
#define GFX_MAX_HEIGHT 1080

static struct limine_framebuffer *fb;

static uint32_t backbuffer[GFX_MAX_WIDTH * GFX_MAX_HEIGHT];

int gfx_init(struct limine_framebuffer *framebuffer)
{
    if (framebuffer == 0 ||
        framebuffer->address == 0 ||
        framebuffer->memory_model != LIMINE_FRAMEBUFFER_RGB ||
        framebuffer->bpp != 32 ||
        framebuffer->width == 0 ||
        framebuffer->height == 0 ||
        framebuffer->width > GFX_MAX_WIDTH ||
        framebuffer->height > GFX_MAX_HEIGHT ||
        framebuffer->pitch < framebuffer->width * 4) {
        fb = 0;
        return 0;
    }

    fb = framebuffer;

    gfx_clear(NC_BLACK);
    gfx_present();

    return 1;
}

uint64_t gfx_width(void)
{
    return fb ? fb->width : 0;
}

uint64_t gfx_height(void)
{
    return fb ? fb->height : 0;
}

void gfx_pixel(int64_t x, int64_t y, uint32_t color)
{
    if (!fb || x < 0 || y < 0 ||
        (uint64_t)x >= fb->width ||
        (uint64_t)y >= fb->height) {
        return;
    }

    backbuffer[(uint64_t)y * fb->width + (uint64_t)x] =
        color & 0xFFFFFF;
}

uint32_t gfx_get_pixel(int64_t x, int64_t y)
{
    if (!fb || x < 0 || y < 0 ||
        (uint64_t)x >= fb->width ||
        (uint64_t)y >= fb->height) {
        return NC_DARK_BLUE;
    }

    return backbuffer[(uint64_t)y * fb->width + (uint64_t)x];
}

static uint32_t pack_color(uint32_t color)
{
    uint32_t r = (color >> 16) & 0xFF;
    uint32_t g = (color >> 8) & 0xFF;
    uint32_t b = color & 0xFF;

    return (r << fb->red_mask_shift) |
           (g << fb->green_mask_shift) |
           (b << fb->blue_mask_shift);
}

void gfx_present_rect(
    int64_t x, int64_t y,
    uint64_t width, uint64_t height
)
{
    if (!fb || width == 0 || height == 0) {
        return;
    }

    int64_t x0 = x < 0 ? 0 : x;
    int64_t y0 = y < 0 ? 0 : y;
    int64_t x1 = x + (int64_t)width;
    int64_t y1 = y + (int64_t)height;

    if (x1 > (int64_t)fb->width) {
        x1 = (int64_t)fb->width;
    }

    if (y1 > (int64_t)fb->height) {
        y1 = (int64_t)fb->height;
    }

    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    for (int64_t row = y0; row < y1; row++) {
        volatile uint32_t *destination =
            (volatile uint32_t *)(
                (uint8_t *)fb->address +
                (uint64_t)row * fb->pitch
            );

        uint64_t source_offset =
            (uint64_t)row * fb->width;

        for (int64_t col = x0; col < x1; col++) {
            destination[col] =
                pack_color(backbuffer[source_offset + (uint64_t)col]);
        }
    }
}

void gfx_present(void)
{
    if (!fb) {
        return;
    }

    gfx_present_rect(0, 0, fb->width, fb->height);
}

void gfx_clear(uint32_t color)
{
    if (!fb) {
        return;
    }

    uint64_t count = fb->width * fb->height;
    color &= 0xFFFFFF;

    for (uint64_t i = 0; i < count; i++) {
        backbuffer[i] = color;
    }
}

void gfx_fill_rect(
    int64_t x, int64_t y,
    uint64_t width, uint64_t height,
    uint32_t color
)
{
    if (!fb) return;

    for (uint64_t row = 0; row < height; row++) {
        for (uint64_t col = 0; col < width; col++) {
            gfx_pixel(x + (int64_t)col,
                      y + (int64_t)row, color);
        }
    }
}

void gfx_hline(
    int64_t x, int64_t y,
    uint64_t length, uint32_t color
)
{
    for (uint64_t i = 0; i < length; i++) {
        gfx_pixel(x + (int64_t)i, y, color);
    }
}

void gfx_vline(
    int64_t x, int64_t y,
    uint64_t length, uint32_t color
)
{
    for (uint64_t i = 0; i < length; i++) {
        gfx_pixel(x, y + (int64_t)i, color);
    }
}

void gfx_rect(
    int64_t x, int64_t y,
    uint64_t width, uint64_t height,
    uint32_t color
)
{
    if (width == 0 || height == 0) return;

    gfx_hline(x, y, width, color);
    gfx_hline(x, y + (int64_t)height - 1, width, color);
    gfx_vline(x, y, height, color);
    gfx_vline(x + (int64_t)width - 1, y, height, color);
}

/* Each glyph is seven rows of five bits.
   The leftmost pixel is bit 4. */
static const uint8_t font[36][7] = {
    {14,17,17,31,17,17,17}, /* A */
    {30,17,17,30,17,17,30}, /* B */
    {14,17,16,16,16,17,14}, /* C */
    {30,17,17,17,17,17,30}, /* D */
    {31,16,16,30,16,16,31}, /* E */
    {31,16,16,30,16,16,16}, /* F */
    {14,17,16,23,17,17,15}, /* G */
    {17,17,17,31,17,17,17}, /* H */
    {14,4,4,4,4,4,14},       /* I */
    {7,2,2,2,18,18,12},      /* J */
    {17,18,20,24,20,18,17},  /* K */
    {16,16,16,16,16,16,31},  /* L */
    {17,27,21,21,17,17,17},  /* M */
    {17,25,21,19,17,17,17},  /* N */
    {14,17,17,17,17,17,14},  /* O */
    {30,17,17,30,16,16,16},  /* P */
    {14,17,17,17,21,18,13},  /* Q */
    {30,17,17,30,20,18,17},  /* R */
    {15,16,16,14,1,1,30},    /* S */
    {31,4,4,4,4,4,4},         /* T */
    {17,17,17,17,17,17,14},  /* U */
    {17,17,17,17,17,10,4},   /* V */
    {17,17,17,21,21,21,10},  /* W */
    {17,17,10,4,10,17,17},   /* X */
    {17,17,10,4,4,4,4},      /* Y */
    {31,1,2,4,8,16,31},      /* Z */

    {14,17,19,21,25,17,14},  /* 0 */
    {4,12,4,4,4,4,14},       /* 1 */
    {14,17,1,2,4,8,31},      /* 2 */
    {30,1,1,14,1,1,30},      /* 3 */
    {2,6,10,18,31,2,2},      /* 4 */
    {31,16,16,30,1,1,30},    /* 5 */
    {14,16,16,30,17,17,14},  /* 6 */
    {31,1,2,4,8,8,8},        /* 7 */
    {14,17,17,14,17,17,14},  /* 8 */
    {14,17,17,15,1,1,14}     /* 9 */
};

static int glyph_index(char c)
{
    if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return 26 + c - '0';
    return -1;
}

static void draw_char(
    int64_t x, int64_t y,
    char c, uint32_t color, uint32_t scale
)
{
    int index = glyph_index(c);

    if (index < 0) return;
    if (scale == 0) scale = 1;

    for (uint32_t row = 0; row < 7; row++) {
        for (uint32_t col = 0; col < 5; col++) {
            if (font[index][row] & (1U << (4 - col))) {
                gfx_fill_rect(
                    x + (int64_t)(col * scale),
                    y + (int64_t)(row * scale),
                    scale, scale, color
                );
            }
        }
    }
}

void gfx_text(
    int64_t x, int64_t y,
    const char *text,
    uint32_t color,
    uint32_t scale
)
{
    if (!text) return;
    if (scale == 0) scale = 1;

    int64_t origin_x = x;

    for (uint64_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\n') {
            x = origin_x;
            y += (int64_t)(9 * scale);
        } else if (text[i] == ' ') {
            x += (int64_t)(4 * scale);
        } else {
            draw_char(x, y, text[i], color, scale);
            x += (int64_t)(6 * scale);
        }

        if (x >= (int64_t)gfx_width()) break;
        if (y >= (int64_t)gfx_height()) break;
    }
}
