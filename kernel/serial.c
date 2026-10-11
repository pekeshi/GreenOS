#include "include/kernel.h"
#include "include/keyboard.h"

#define COM1 0x3F8

static void outb(u16 port, u8 value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static u8 inb(u16 port) {
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

void serial_poll_input(void) {
    int input;
    while ((input = serial_getchar()) >= 0) {
        if (input == '\r' || input == '\n') {
            keyboard_push_key(KEY_ENTER);
        } else if (input == '\b' || input == 0x7F) {
            keyboard_push_key(KEY_BACKSPACE);
        } else if (input >= 0x20 && input < 0x7F) {
            keyboard_push_char((char)input);
        }
    }
}

void serial_putc(char c) {
    if (c == '\n') {
        serial_putc('\r');
    }
    while ((inb(COM1 + 5) & 0x20) == 0) {
        __asm__ volatile("pause");
    }
    outb(COM1, (u8)c);
}

void serial_puts(const char *text) {
    while (*text != 0) {
        serial_putc(*text++);
    }
}

int serial_getchar(void) {
    if ((inb(COM1 + 5) & 0x01) == 0) {
        return -1;
    }
    return inb(COM1);
}

void keyboard_wait_for_input(void) {
    __asm__ volatile("sti; hlt" : : : "memory");
}
