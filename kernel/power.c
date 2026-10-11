#include "include/kernel.h"
#include "include/vga.h"

static void outb(u16 port, u8 value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void outw(u16 port, u16 value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static u8 inb(u16 port) {
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void halt_forever(void) {
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

void kernel_poweroff(void) {
    __asm__ volatile("cli");

    outw(0x0604, 0x2000);
    outw(0xB004, 0x2000);

    vga_puts("Shutdown request was not accepted; CPU halted.\n");
    halt_forever();
}

void kernel_reboot(void) {
    __asm__ volatile("cli");

    outb(0x0CF9, 0x06);

    for (u32 attempt = 0; attempt < 100000; attempt++) {
        if ((inb(0x0064) & 0x02) == 0) {
            outb(0x0064, 0xFE);
            break;
        }
        __asm__ volatile("pause");
    }

    vga_puts("Reset request was not accepted; CPU halted.\n");
    halt_forever();
}
