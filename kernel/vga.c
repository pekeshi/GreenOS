#include "vga.h"
#include <stdarg.h>

void vga_init(void) {
    vga_clear();
}

static void serial_put_decimal(unsigned value) {
    char digits[3];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0) {
        serial_putc(digits[--count]);
    }
}

void vga_set_color(u8 fg, u8 bg) {
    static const u8 ansi_colors[16] = {
        30, 34, 32, 36, 31, 35, 33, 37,
        90, 94, 92, 96, 91, 95, 93, 97
    };
    u8 foreground = ansi_colors[fg & 0x0F];
    u8 background = (u8)(((bg & 0x08) ? 100 : 40) + (bg & 0x07));
    framebuffer_console_set_color(fg, bg);
    serial_putc('\x1b');
    serial_putc('[');
    serial_put_decimal(foreground);
    serial_putc(';');
    serial_put_decimal(background);
    serial_putc('m');
}

void vga_clear(void) {
    vga_puts("\x1b[2J\x1b[H");
    framebuffer_console_clear();
}

void vga_putc(char c) {
    if (c == '\b') {
        serial_putc('\b');
        serial_putc(' ');
        serial_putc('\b');
        framebuffer_console_putc(c);
        return;
    }
    serial_putc(c);
    framebuffer_console_putc(c);
}

void vga_puts(const char *s) {
    while (*s) {
        vga_putc(*s++);
    }
}

// Print formatted output to the console
static void print_uint(u64 value, int base, int uppercase) {
    char buffer[32]; // Buffer to hold the converted number as a string
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef"; // Choose the appropriate digit characters based on the uppercase flag
    int i = 0;
    if (value == 0) {                       // Handle zero case
        vga_putc('0');                      // Print '0' for zero value
        return;                             // Exit the function early since we don't need to convert further
    }
    while (value) {                         // Convert the number to the specified base
        buffer[i++] = digits[value % base]; // Get the corresponding digit character, by using the modulo operator to find the remainder when dividing by the base
        value = value / base;               // Divide the value by the base for the next iteration
    }
    while (i--) {
        vga_putc(buffer[i]);
    }
}

static void print_int(i64 value) {
    if (value < 0) { 
        vga_putc('-'); value = -value;
    }
    print_uint((u64)value, 10, 0);
}

void vga_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    while (*fmt) {
        if (*fmt != '%') {
            vga_putc(*fmt++);
            continue;
        }
        fmt++; // Skip the '%' character
        switch (*fmt) {
            case 'c': {
                vga_putc((char)va_arg(args, int));
                break;
            }
            case 's': {
                vga_puts(va_arg(args, char *));
                break;
            }
            case 'd': {
                case 'i': {
                    print_int(va_arg(args, i64));
                    break;
                }
            }
            case 'u': {
                print_uint(va_arg(args, unsigned int), 10, 0);
                break;
            }
            case 'x': {
                print_uint(va_arg(args, unsigned int), 16, 0);
                break;
            }
            case 'X': {
                print_uint(va_arg(args, unsigned int), 16, 1);
                break;
            }
            case 'p': {
                vga_puts("0x");
                print_uint((u64)va_arg(args, void *), 16, 0);
                break;
            }
            case '%': {
                vga_putc('%');
                break;
            }
            default: {
                vga_putc('%');
                vga_putc(*fmt);
                break;
            }
        }
        fmt++;
    }
    va_end(args);
}
