#include <stdint.h>
#include <stddef.h>
#include "limine.h"

/* CosmOS Limine boot protocol requests */
static volatile uint64_t limine_base_revision[3]
__attribute__((used, section(".limine_requests"))) = LIMINE_BASE_REVISION(3);
static volatile uint64_t limine_requests_start_marker[4]
__attribute__((used, section(".limine_requests_start"))) = LIMINE_REQUESTS_START_MARKER;
static volatile uint64_t limine_requests_end_marker[2]
__attribute__((used, section(".limine_requests_end"))) = LIMINE_REQUESTS_END_MARKER;

static volatile struct limine_framebuffer_request framebuffer_request
__attribute__((used, section(".limine_requests"))) = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

static uint32_t *pixels;
static uint64_t screen_width, screen_height, pitch;

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline void cpu_pause(void) { __asm__ volatile ("pause"); }

static uint32_t background_pixel(uint64_t x, uint64_t y) {
    uint8_t blue = 35 + (uint8_t)((y * 30) / screen_height);
    (void)x;
    return 0xFF000000u | (12u << 16) | (15u << 8) | blue;
}
static void put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || (uint64_t)x >= screen_width || (uint64_t)y >= screen_height) return;
    pixels[(uint64_t)y * pitch + (uint64_t)x] = color;
}
static void restore_background(int x, int y) {
    if (x < 0 || y < 0 || (uint64_t)x >= screen_width || (uint64_t)y >= screen_height) return;
    pixels[(uint64_t)y * pitch + (uint64_t)x] = background_pixel((uint64_t)x, (uint64_t)y);
}

/* High-contrast software arrow cursor. */
static const char *cursor_shape[18] = {
    "X...............", "XX..............", "XWX.............",
    "XWWX............", "XWWWX...........", "XWWWWX..........",
    "XWWWWWX.........", "XWWWWWWX........", "XWWWWWWWX.......",
    "XWWWWXXXXX......", "XWXWWX..........", "XX.XWWX.........",
    "X...XWWX........", ".....XWWX.......", "......XWWX......",
    ".......XX.......", "................", "................"
};
#define CURSOR_WIDTH 16
#define CURSOR_HEIGHT 18
static int cursor_x, cursor_y;

static void draw_cursor(void) {
    for (int y = 0; y < CURSOR_HEIGHT; y++)
        for (int x = 0; x < CURSOR_WIDTH; x++) {
            char c = cursor_shape[y][x];
            if (c == 'X') put_pixel(cursor_x + x, cursor_y + y, 0xFF05050Au);
            else if (c == 'W') put_pixel(cursor_x + x, cursor_y + y, 0xFFFFFFFFu);
        }
}
static void erase_cursor(void) {
    for (int y = 0; y < CURSOR_HEIGHT; y++)
        for (int x = 0; x < CURSOR_WIDTH; x++)
            if (cursor_shape[y][x] != '.') restore_background(cursor_x + x, cursor_y + y);
}

static int wait_input_empty(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if ((inb(0x64) & 0x02u) == 0) return 1;
        cpu_pause();
    }
    return 0;
}

/* Read the controller's own output (bit 5 clear), not mouse/AUX data. */
static int read_controller_data(uint8_t *value) {
    for (uint32_t i = 0; i < 100000; i++) {
        uint8_t status = inb(0x64);
        if ((status & 0x01u) && !(status & 0x20u)) {
            *value = inb(0x60);
            return 1;
        }
        cpu_pause();
    }
    return 0;
}
static int wait_aux_output(uint8_t *value) {
    for (uint32_t i = 0; i < 100000; i++) {
        uint8_t status = inb(0x64);
        if ((status & 0x21u) == 0x21u) {
            *value = inb(0x60);
            return 1;
        }
        cpu_pause();
    }
    return 0;
}
static int mouse_command(uint8_t command) {
    uint8_t response;
    if (!wait_input_empty()) return 0;
    outb(0x64, 0xD4);
    if (!wait_input_empty()) return 0;
    outb(0x60, command);
    if (!wait_aux_output(&response)) return 0;
    return response == 0xFA;
}

/* Initialize the legacy PS/2 mouse for polled (IRQ-free) operation. */
static int initialize_mouse(void) {
    uint8_t command_byte;

    if (!wait_input_empty()) return 0;
    outb(0x64, 0xA8); /* Enable auxiliary port. */

    if (!wait_input_empty()) return 0;
    outb(0x64, 0x20); /* Read controller configuration byte. */
    if (!read_controller_data(&command_byte)) return 0;

    /* Clear IRQ12-enable and auxiliary-clock-disable; poll the device instead. */
    command_byte &= (uint8_t)~0x22u;
    if (!wait_input_empty()) return 0;
    outb(0x64, 0x60);
    if (!wait_input_empty()) return 0;
    outb(0x60, command_byte);

    if (!mouse_command(0xF6)) return 0; /* Set defaults. */
    if (!mouse_command(0xF4)) return 0; /* Enable data reporting. */
    return 1;
}

/* Consume every controller byte so keyboard data cannot clog the output buffer. */
static int read_controller_byte(uint8_t *value, int *is_mouse) {
    uint8_t status = inb(0x64);
    if (!(status & 0x01u)) return 0;
    *is_mouse = (status & 0x20u) != 0;
    *value = inb(0x60);
    return 1;
}

static void run_mouse_cursor(void) {
    uint8_t packet[3];
    unsigned packet_index = 0;

    cursor_x = (int)(screen_width / 2);
    cursor_y = (int)(screen_height / 2);
    if (cursor_x > (int)screen_width - CURSOR_WIDTH) cursor_x = 0;
    if (cursor_y > (int)screen_height - CURSOR_HEIGHT) cursor_y = 0;
    draw_cursor();

    /* If no PS/2 device is present, keep the desktop alive with a visible cursor. */
    if (!initialize_mouse()) {
        for (;;) cpu_pause();
    }

    for (;;) {
        uint8_t data;
        int is_mouse;
        if (!read_controller_byte(&data, &is_mouse)) {
            cpu_pause();
            continue;
        }
        if (!is_mouse) continue; /* Discard keyboard bytes until keyboard support exists. */

        /* Synchronize on the PS/2 packet's always-set first-byte bit. */
        if (packet_index == 0 && (data & 0x08u) == 0) continue;
        packet[packet_index++] = data;
        if (packet_index < 3) continue;
        packet_index = 0;

        if (packet[0] & 0xC0u) continue; /* Ignore overflow packets. */
        int dx = (int)(int8_t)packet[1];
        int dy = (int)(int8_t)packet[2];
        if (dx == 0 && dy == 0) continue;

        erase_cursor();
        cursor_x += dx;
        cursor_y -= dy;
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_y < 0) cursor_y = 0;
        if (cursor_x > (int)screen_width - CURSOR_WIDTH) cursor_x = (int)screen_width - CURSOR_WIDTH;
        if (cursor_y > (int)screen_height - CURSOR_HEIGHT) cursor_y = (int)screen_height - CURSOR_HEIGHT;
        draw_cursor();
    }
}

void _start(void) {
    struct limine_framebuffer_response *response = framebuffer_request.response;
    if (response == NULL || response->framebuffer_count == 0)
        for (;;) __asm__ volatile ("cli; hlt");

    struct limine_framebuffer *fb = response->framebuffers[0];
    if (fb == NULL || fb->address == NULL || fb->bpp != 32 ||
        fb->width < CURSOR_WIDTH || fb->height < CURSOR_HEIGHT)
        for (;;) __asm__ volatile ("cli; hlt");

    pixels = (uint32_t *)fb->address;
    screen_width = fb->width;
    screen_height = fb->height;
    pitch = fb->pitch / sizeof(uint32_t);

    for (uint64_t y = 0; y < screen_height; y++)
        for (uint64_t x = 0; x < screen_width; x++)
            pixels[y * pitch + x] = background_pixel(x, y);

    run_mouse_cursor();
}
