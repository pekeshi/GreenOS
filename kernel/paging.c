#include "include/kernel.h"

#define PAGE_SIZE 4096ULL
#define PAGE_DIRECTORY_ENTRIES 512
#define IDENTITY_MAP_SIZE (16ULL * 1024 * 1024 * 1024)
#define PAGE_PRESENT_WRITE 0x003ULL
#define PAGE_SIZE_FLAG 0x080ULL

static u64 *allocate_table(void) {
    u64 *table = (u64 *)page_allocate();
    if (table == 0) {
        return 0;
    }
    for (u64 i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
        table[i] = 0;
    }
    return table;
}

void paging_init(void) {
    u64 *pml4 = allocate_table();
    u64 *pdpt = allocate_table();
    if (pml4 == 0 || pdpt == 0) {
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    }

    pml4[0] = (u64)(uintptr_t)pdpt | PAGE_PRESENT_WRITE;
    u64 directory_count = IDENTITY_MAP_SIZE / (1ULL << 30);

    for (u64 directory_index = 0; directory_index < directory_count; directory_index++) {
        u64 *directory = allocate_table();
        if (directory == 0) {
            for (;;) {
                __asm__ volatile("cli; hlt");
            }
        }
        pdpt[directory_index] = (u64)(uintptr_t)directory | PAGE_PRESENT_WRITE;

        for (u64 entry_index = 0; entry_index < PAGE_DIRECTORY_ENTRIES; entry_index++) {
            u64 physical = (directory_index * PAGE_DIRECTORY_ENTRIES + entry_index)
                * (2ULL * 1024 * 1024);
            directory[entry_index] = physical | PAGE_PRESENT_WRITE | PAGE_SIZE_FLAG;
        }
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"((u64)(uintptr_t)pml4) : "memory");
}
