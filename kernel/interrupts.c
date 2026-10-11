#include "include/kernel.h"

typedef struct __attribute__((packed)) {
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 attributes;
    u16 offset_middle;
    u32 offset_high;
    u32 reserved;
} IDT_GATE;

typedef struct __attribute__((packed)) {
    u16 limit;
    u64 base;
} IDT_REGISTER;

static IDT_GATE idt[256] __attribute__((aligned(16)));
static volatile u64 timer_ticks;

extern void isr_default(void);
extern void isr_timer(void);
extern void isr_keyboard(void);

static void outb(u16 port, u8 value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void io_wait(void) {
    outb(0x80, 0);
}

static void set_gate(u8 vector, void (*handler)(void), u16 selector) {
    u64 address = (u64)(uintptr_t)handler;
    idt[vector].offset_low = (u16)address;
    idt[vector].selector = selector;
    idt[vector].ist = 0;
    idt[vector].attributes = 0x8E;
    idt[vector].offset_middle = (u16)(address >> 16);
    idt[vector].offset_high = (u32)(address >> 32);
    idt[vector].reserved = 0;
}

static void remap_and_mask_pic(void) {
    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();
    outb(0x21, 0x20);
    io_wait();
    outb(0xA1, 0x28);
    io_wait();
    outb(0x21, 0x04);
    io_wait();
    outb(0xA1, 0x02);
    io_wait();
    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();

    if (!keyboard_controller_init()) {
        serial_puts("\nWarning: PS/2 keyboard not available.\n");
    }
    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
}

static void initialize_pit(void) {
    const u32 frequency = 100;
    const u16 divisor = (u16)(1193182 / frequency);
    outb(0x43, 0x36);
    outb(0x40, (u8)divisor);
    outb(0x40, (u8)(divisor >> 8));
}

void interrupts_init(void) {
    u16 code_segment;
    __asm__ volatile("mov %%cs, %0" : "=r"(code_segment));

    for (u16 vector = 0; vector < 256; vector++) {
        set_gate((u8)vector, isr_default, code_segment);
    }
    set_gate(32, isr_timer, code_segment);
    set_gate(33, isr_keyboard, code_segment);

    IDT_REGISTER idtr = {
        .limit = sizeof(idt) - 1,
        .base = (u64)(uintptr_t)idt
    };
    __asm__ volatile("lidt %0" : : "m"(idtr));

    remap_and_mask_pic();
    initialize_pit();
    __asm__ volatile("sti");
}

void kernel_timer_tick(void) {
    timer_ticks++;
    serial_poll_input();
    outb(0x20, 0x20);
}

u64 timer_ticks_since_boot(void) {
    return timer_ticks;
}
