#ifndef VGA_H
#define VGA_H

#include "kernel.h"

void vga_init(void);
void vga_clear(void);
void vga_putc(char c);
void vga_puts(const char *s);
void vga_printf(const char *fmt, ...);
void vga_set_color(u8 fg, u8 bg);

#endif