
#include <stdint.h>
#include <stddef.h>
#include "limine.h"

/* CosmOS Limine boot protocol requests */

static volatile uint64_t limine_base_revision[3]
__attribute__((used, section(".limine_requests"))) =
    LIMINE_BASE_REVISION(3);

static volatile uint64_t limine_requests_start_marker[4]
__attribute__((used, section(".limine_requests_start"))) =
    LIMINE_REQUESTS_START_MARKER;

static volatile uint64_t limine_requests_end_marker[2]
__attribute__((used, section(".limine_requests_end"))) =
    LIMINE_REQUESTS_END_MARKER;

/* Framebuffer request */

static volatile struct limine_framebuffer_request framebuffer_request
__attribute__((used, section(".limine_requests"))) = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

/* Stop the CPU safely */

static void halt_forever(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

/* CosmOS kernel entry point */

void _start(void) {
    struct limine_framebuffer_response *response =
        framebuffer_request.response;

    if (response == NULL ||
        response->framebuffer_count == 0) {
        halt_forever();
    }

    struct limine_framebuffer *fb =
        response->framebuffers[0];

    if (fb == NULL || fb->address == NULL) {
        halt_forever();
    }

    uint32_t *pixels = (uint32_t *)fb->address;

    uint64_t width = fb->width;
    uint64_t height = fb->height;
    uint64_t pitch = fb->pitch / sizeof(uint32_t);

    /* Draw CosmOS dark blue background */

    for (uint64_t y = 0; y < height; y++) {
        for (uint64_t x = 0; x < width; x++) {

            uint8_t blue =
                35 + (uint8_t)((y * 30) / height);

            pixels[y * pitch + x] =
                (0xFFu << 24) |
                (12u << 16) |
                (15u << 8) |
                blue;
        }
    }

    /* Keep CosmOS running */

    halt_forever();
}
