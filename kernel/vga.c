#include "vga.h"
#include <stdarg.h>

// VGA text mode dimensions
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

// VGA buffer pointer
static volatile u16 *vga_buffer = (u16 *)0xB8000;
static int cursor_x = 0;
static int cursor_y = 0;
static u8 color = 0x07; // Default color: light gray on black

// Initialize the VGA text mode
void vga_init(void) {
    vga_clear();
}

// Set the color for text output
void vga_set_color(u8 fg, u8 bg) {
    color = (bg << 4) | (fg & 0x0F); // Set the color attribute for text output
}

// Fill the screen with spaces and the current color
void vga_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = (color << 8) | ' '; // Fill the screen with spaces and the current color
    }
    cursor_x = 0;
    cursor_y = 0;
}

static void scroll(void) {
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = (color << 8) | ' ';
    }
    cursor_y = VGA_HEIGHT - 1;
}

void vga_putc(char c) {
    if ( c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = color << 8 | ' ';
        }
    } else if (c == '\t') {
        cursor_x = (cursor_x + 4) $ ~3;
    } else {
        vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = (color << 8) | (u8)c;
    }
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }
    if (cursor_y >= VGA_HEIGHT) {
        scroll();
    }
}

void vga_puts(const char *s) {
    while (*s) {
        vga_putc(*s++);
    }
}

// Print formatted output to the VGA text mode
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
                print_uint(va_arg(args, u64), 10, 0);
                break;
            }
            case 'x': {
                print_uint(va_arg(args, u64), 16, 0);
                break;
            }
            case 'X': {
                print_uint(va_arg(args, u64), 16, 1);
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
