#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stddef.h>
#include "boot_info.h"

#define GREENYOS_VERSION "0.1.2"
#define KERNEL_IDENTITY_MAP_SIZE (1024ULL * 1024 * 1024 * 1024)

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;
typedef int64_t  i64;
typedef int32_t  i32;
typedef int16_t  i16;
typedef int8_t   i8;

void kernel_main(GreenyOSBootInfo *boot_info);
void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *text);
int serial_getchar(void);
void serial_poll_input(void);
void kernel_poweroff(void);
void kernel_reboot(void);
void interrupts_init(void);
u64 timer_ticks_since_boot(void);
void gdt_init(void);
void paging_init(void);
int paging_mark_uncached(u64 physical_address, u64 length);
void memory_init(const GreenyOSBootInfo *boot_info);
u64 memory_free_pages(void);
void *page_allocate(void);
void page_free(void *address);
void keyboard_wait_for_input(void);
int keyboard_controller_init(void);
int usb_keyboard_init(void);
void usb_keyboard_poll(void);
void framebuffer_init(const GreenyOSFramebuffer *framebuffer);
void framebuffer_clear(u8 red, u8 green, u8 blue);
void framebuffer_fill_rect(
    u32 x, u32 y, u32 width, u32 height, u8 red, u8 green, u8 blue);
void framebuffer_console_init(void);
void framebuffer_console_clear(void);
void framebuffer_console_putc(char c);
void framebuffer_console_scroll_page(int direction);
void framebuffer_console_set_color(u8 fg, u8 bg);
u32 framebuffer_console_theme_count(void);
const char *framebuffer_console_theme_name(u32 index);
int framebuffer_console_set_theme(const char *name);

#endif