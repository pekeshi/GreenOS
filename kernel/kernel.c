#include "include/kernel.h"
#include "include/keyboard.h"
#include "include/shell.h"
#include "include/vga.h"

void kernel_main(GreenyOSBootInfo *boot_info) {
    serial_init();
    if (boot_info == 0 || boot_info->version != GREENYOS_BOOT_INFO_VERSION ||
        boot_info->size < sizeof(GreenyOSBootInfo)) {
        serial_puts("Invalid boot info; halting.\n");
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    }

    framebuffer_init(&boot_info->framebuffer);
    framebuffer_console_init();
    vga_init();
    vga_puts("GreenyOS kernel\n");
    vga_printf("Boot handoff v%u received.\n", boot_info->version);
    vga_printf("Framebuffer: %ux%u at %p.\n",
        boot_info->framebuffer.width,
        boot_info->framebuffer.height,
        (void *)(uintptr_t)boot_info->framebuffer.address);

    memory_init(boot_info);
    vga_printf("Memory map accepted; %u pages available.\n",
        (unsigned int)memory_free_pages());
    u64 available_pages = memory_free_pages();
    void *test_page = page_allocate();
    if (test_page == 0) {
        vga_puts("Page allocator test failed; halting.\n");
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    }
    page_free(test_page);
    if (memory_free_pages() != available_pages) {
        vga_puts("Page allocator accounting failed; halting.\n");
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    }
    vga_puts("Physical page allocation test passed.\n");
    paging_init();
    vga_puts("Kernel-owned identity page tables installed.\n");
    gdt_init();
    vga_puts("Kernel GDT installed.\n");
    interrupts_init();
    vga_puts("Kernel IDT installed; PIT timer interrupt running at 100 Hz.\n");

    keyboard_init();
    if (usb_keyboard_init()) {
        vga_puts("Native USB keyboard ready (xHCI).\n");
    } else {
        vga_puts("No native USB keyboard found; PS/2/serial input remains available.\n");
    }
    shell_init();
    vga_puts("Shell is on the framebuffer.\n");
    shell_run();
}
