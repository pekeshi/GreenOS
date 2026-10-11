#include "include/kernel.h"

typedef struct __attribute__((packed)) {
    u16 limit;
    u64 base;
} GDT_REGISTER;

static u64 gdt[] __attribute__((aligned(16))) = {
    0,
    0x00AF9A000000FFFF,
    0x00CF92000000FFFF
};

extern void gdt_load(const GDT_REGISTER *gdtr);

void gdt_init(void) {
    GDT_REGISTER gdtr = {
        .limit = sizeof(gdt) - 1,
        .base = (u64)(uintptr_t)gdt
    };
    gdt_load(&gdtr);
}
