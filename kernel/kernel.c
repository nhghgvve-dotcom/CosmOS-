#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

/* Tell Limine which protocol revision this kernel supports. */
__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

/* Request a framebuffer from the bootloader so we can draw graphics. */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

/* Define the start and end markers for the Limine requests. */
__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

/* Safe CPU halt loop function */
static void halt(void) {
    for (;;) {
        __asm__("hlt");
    }
}

/* The main entry point where CosmOS begins execution */
void _start(void) {
    // 1. Ensure the bootloader successfully gave us a framebuffer response
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        halt();
    }

    // 2. Fetch the primary screen buffer details
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    // 3. Clear the screen to a beautiful solid background color (Deep Navy Blue)
    // NOTE: This treats pixels as standard 32-bit (ARGB/XRGB) colors
    uint32_t *fb_ptr = (uint32_t *)framebuffer->address;
    size_t total_pixels = (framebuffer->pitch / 4) * framebuffer->height;
    
    for (size_t i = 0; i < total_pixels; i++) {
        fb_ptr[i] = 0x1E3C72; // Hex code for your custom OS dark blue background!
    }

    // 4. Kernel operations completed successfully, enter safe loop
    halt();
}
