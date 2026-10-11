#ifndef NEPTUNE_GRAPHICS_H
#define NEPTUNE_GRAPHICS_H

#include <stdint.h>
#include <limine.h>

/* Neptune OS: fixed 16-color palette, RGB hexadecimal values. */
#define NC_RED       0xFF0000
#define NC_GREEN     0x00FF00
#define NC_BLUE      0x0000FF
#define NC_YELLOW    0xFFFF00
#define NC_MAGENTA   0xFF00FF
#define NC_CYAN      0x00FFFF
#define NC_DARK_RED  0x800000
#define NC_DARK_GREEN 0x008000
#define NC_DARK_BLUE 0x000080
#define NC_OLIVE     0x808000
#define NC_PURPLE    0x800080
#define NC_TEAL      0x008080
#define NC_BLACK     0x000000
#define NC_GRAY      0x808080
#define NC_SILVER    0xC0C0C0
#define NC_WHITE     0xFFFFFF

int gfx_init(struct limine_framebuffer *framebuffer);

uint64_t gfx_width(void);
uint64_t gfx_height(void);

uint32_t gfx_get_pixel(int64_t x, int64_t y);

void gfx_present(void);

void gfx_wait_vsync(void);

void gfx_present_rect(
    int64_t x, int64_t y,
    uint64_t width, uint64_t height
);

void gfx_pixel(int64_t x, int64_t y, uint32_t color);
void gfx_clear(uint32_t color);

void gfx_fill_rect(
    int64_t x, int64_t y,
    uint64_t width, uint64_t height,
    uint32_t color
);

void gfx_rect(
    int64_t x, int64_t y,
    uint64_t width, uint64_t height,
    uint32_t color
);

void gfx_hline(
    int64_t x, int64_t y,
    uint64_t length, uint32_t color
);

void gfx_vline(
    int64_t x, int64_t y,
    uint64_t length, uint32_t color
);

/* Text uses a built-in bitmap font and supports uppercase letters,
   digits, spaces, and basic punctuation. */
void gfx_text(
    int64_t x, int64_t y,
    const char *text,
    uint32_t color,
    uint32_t scale
);

#endif
