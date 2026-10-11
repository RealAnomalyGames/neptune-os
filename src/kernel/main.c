#include <stdint.h>
#include <limine.h>

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] =
    LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] =
    LIMINE_REQUESTS_END_MARKER;

static void halt(void)
{
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void put_pixel(
    struct limine_framebuffer *fb,
    uint64_t x,
    uint64_t y,
    uint32_t color
)
{
    if (x >= fb->width || y >= fb->height || fb->bpp != 32) {
        return;
    }

    volatile uint32_t *pixel =
        (volatile uint32_t *)((uint8_t *)fb->address
        + y * fb->pitch + x * 4);

    uint32_t r = (color >> 16) & 0xFF;
    uint32_t g = (color >> 8) & 0xFF;
    uint32_t b = color & 0xFF;

    uint32_t packed =
        (r << fb->red_mask_shift) |
        (g << fb->green_mask_shift) |
        (b << fb->blue_mask_shift);

    *pixel = packed;
}

static void fill_screen(
    struct limine_framebuffer *fb,
    uint32_t color
)
{
    for (uint64_t y = 0; y < fb->height; y++) {
        for (uint64_t x = 0; x < fb->width; x++) {
            put_pixel(fb, x, y, color);
        }
    }
}

void _start(void)
{
    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision)) {
        halt();
    }

    struct limine_framebuffer_response *response =
        framebuffer_request.response;

    if (response == 0 || response->framebuffer_count == 0) {
        halt();
    }

    struct limine_framebuffer *fb =
        response->framebuffers[0];

    if (fb == 0 || fb->memory_model != LIMINE_FRAMEBUFFER_RGB
        || fb->bpp != 32) {
        halt();
    }

    fill_screen(fb, 0x000080);

    /* Centered white boot-success panel. */
    uint64_t panel_w = 240;
    uint64_t panel_h = 80;
    uint64_t left = (fb->width - panel_w) / 2;
    uint64_t top = (fb->height - panel_h) / 2;

    for (uint64_t y = top; y < top + panel_h; y++) {
        for (uint64_t x = left; x < left + panel_w; x++) {
            put_pixel(fb, x, y, 0xFFFFFF);
        }
    }

    /* Navy center mark. */
    for (uint64_t y = top + 20; y < top + 60; y++) {
        for (uint64_t x = left + 20; x < left + 60; x++) {
            put_pixel(fb, x, y, 0x000080);
        }
    }

    /* Green success indicator. */
    for (uint64_t y = top + 30; y < top + 50; y++) {
        for (uint64_t x = left + 80; x < left + 100; x++) {
            put_pixel(fb, x, y, 0x00FF00);
        }
    }

    for (uint64_t y = top + 40; y < top + 50; y++) {
        for (uint64_t x = left + 100; x < left + 120; x++) {
            put_pixel(fb, x, y, 0x00FF00);
        }
    }

    halt();
}
