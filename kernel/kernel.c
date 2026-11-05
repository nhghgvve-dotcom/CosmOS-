#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

/* Tell Limine which protocol revision this kernel supports. */
__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

static void halt_forever(void) {
    for (;;) __asm__ volatile ("cli; hlt");
}

static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void put_pixel(struct limine_framebuffer *fb, uint64_t x, uint64_t y, uint32_t color) {
    if (x >= fb->width || y >= fb->height || fb->bpp != 32) return;
    volatile uint32_t *row = (volatile uint32_t *)((uintptr_t)fb->address + y * fb->pitch);
    row[x] = color;
}

/* Small 5x7 bitmap font for the boot screen. */
static const uint8_t font[37][5] = {
    {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
    {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
    {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
    {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
    {0x62,0x51,0x49,0x49,0x46}, {0x22,0x41,0x49,0x49,0x36},
    {0x18,0x14,0x12,0x7f,0x10}, {0x2f,0x49,0x49,0x49,0x31},
    {0x3e,0x49,0x49,0x49,0x32}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x26,0x49,0x49,0x49,0x3e},
    {0,0,0,0,0}
};

static int glyph_index(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return 26 + c - '0';
    return 36;
}

static void draw_text(struct limine_framebuffer *fb, uint64_t x, uint64_t y,
                      const char *s, uint32_t color, unsigned scale) {
    for (; *s; ++s, x += 6 * scale) {
        int gi = glyph_index(*s);
        for (unsigned cx = 0; cx < 5; ++cx) {
            for (unsigned cy = 0; cy < 7; ++cy) {
                if (font[gi][cx] & (1u << cy)) {
                    for (unsigned dx = 0; dx < scale; ++dx)
                        for (unsigned dy = 0; dy < scale; ++dy)
                            put_pixel(fb, x + cx * scale + dx, y + cy * scale + dy, color);
                }
            }
        }
    }
}

static void draw_boot_screen(struct limine_framebuffer *fb) {
    uint64_t w = fb->width, h = fb->height;
    /* Deep-space gradient background. */
    for (uint64_t y = 0; y < h; ++y) {
        for (uint64_t x = 0; x < w; ++x) {
            uint8_t r = 5 + (uint8_t)(20 * x / (w ? w : 1));
            uint8_t g = 5 + (uint8_t)(8 * y / (h ? h : 1));
            uint8_t b = 24 + (uint8_t)(48 * (w - x) / (w ? w : 1));
            put_pixel(fb, x, y, rgb(r, g, b));
        }
    }
    /* Deterministic stars. */
    uint32_t seed = 0xC05A05u;
    for (unsigned i = 0; i < 180; ++i) {
        seed = seed * 1664525u + 1013904223u;
        uint64_t x = seed % w;
        seed = seed * 1664525u + 1013904223u;
        uint64_t y = seed % h;
        put_pixel(fb, x, y, rgb(145, 160, 255));
        if ((i % 13) == 0) put_pixel(fb, x + (x + 1 < w), y, rgb(220, 220, 255));
    }
    const char *title = "COSMOS";
    unsigned scale = (w >= 1000) ? 8 : 5;
    uint64_t title_w = 6u * scale * 6u;
    draw_text(fb, (w - title_w) / 2, h / 2 - 55, title, rgb(238, 235, 255), scale);
    const char *sub = "INDEPENDENT OS PROTOTYPE";
    unsigned subscale = (w >= 1000) ? 2 : 1;
    uint64_t subw = 6u * subscale * 24u;
    draw_text(fb, (w - subw) / 2, h / 2 + 35, sub, rgb(155, 170, 235), subscale);
    const char *status = "KERNEL ONLINE";
    uint64_t sw = 6u * 2u * 13u;
    draw_text(fb, (w - sw) / 2, h / 2 + 75, status, rgb(125, 220, 210), 2);
}

void _start(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED) halt_forever();
    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) halt_forever();

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    if (fb == NULL || fb->address == NULL || fb->bpp != 32) halt_forever();

    draw_boot_screen(fb);
    halt_forever();
}
