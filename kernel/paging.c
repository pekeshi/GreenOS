#include "include/kernel.h"

#define PAGE_SIZE 4096ULL
#define PAGE_DIRECTORY_ENTRIES 512
#define PAGE_PRESENT_WRITE 0x003ULL
#define PAGE_WRITE_THROUGH 0x008ULL
#define PAGE_CACHE_DISABLE 0x010ULL
#define PAGE_SIZE_FLAG 0x080ULL
#define UNCACHED_MAP_START (16ULL * 1024 * 1024 * 1024)

static u64 *page_directories[
    KERNEL_IDENTITY_MAP_SIZE / (1ULL * 1024 * 1024 * 1024)];

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
    u64 *pdpt_low = allocate_table();
    u64 *pdpt_high = allocate_table();
    if (pml4 == 0 || pdpt_low == 0 || pdpt_high == 0) {
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    }

    pml4[0] = (u64)(uintptr_t)pdpt_low | PAGE_PRESENT_WRITE;
    pml4[1] = (u64)(uintptr_t)pdpt_high | PAGE_PRESENT_WRITE;
    u64 directory_count = KERNEL_IDENTITY_MAP_SIZE / (1ULL << 30);

    for (u64 directory_index = 0; directory_index < directory_count; directory_index++) {
        u64 *directory = allocate_table();
        if (directory == 0) {
            for (;;) {
                __asm__ volatile("cli; hlt");
            }
        }
        page_directories[directory_index] = directory;
        u64 *pdpt = directory_index < PAGE_DIRECTORY_ENTRIES
            ? pdpt_low
            : pdpt_high;
        u64 pdpt_index = directory_index % PAGE_DIRECTORY_ENTRIES;
        pdpt[pdpt_index] = (u64)(uintptr_t)directory | PAGE_PRESENT_WRITE;

        for (u64 entry_index = 0; entry_index < PAGE_DIRECTORY_ENTRIES; entry_index++) {
            u64 physical = (directory_index * PAGE_DIRECTORY_ENTRIES + entry_index)
                * (2ULL * 1024 * 1024);
            u64 cache_flags = physical >= UNCACHED_MAP_START
                ? PAGE_WRITE_THROUGH | PAGE_CACHE_DISABLE
                : 0;
            directory[entry_index] = physical | PAGE_PRESENT_WRITE |
                PAGE_SIZE_FLAG | cache_flags;
        }
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"((u64)(uintptr_t)pml4) : "memory");
}

int paging_mark_uncached(u64 physical_address, u64 length) {
    if (length == 0 || physical_address >= KERNEL_IDENTITY_MAP_SIZE ||
        length > KERNEL_IDENTITY_MAP_SIZE - physical_address) {
        return 0;
    }

    u64 first_page = physical_address & ~(PAGE_SIZE - 1);
    u64 last_page = (physical_address + length - 1) & ~(PAGE_SIZE - 1);
    for (u64 page = first_page; ; page += PAGE_SIZE) {
        u64 directory_index = page / (1ULL * 1024 * 1024 * 1024);
        if (page_directories[directory_index] == 0) {
            return 0;
        }
        if (page == last_page) {
            break;
        }
    }

    __asm__ volatile("wbinvd" : : : "memory");

    int updated = 0;
    int completed = 0;
    for (u64 page = first_page; ; page += PAGE_SIZE) {
        u64 directory_index = page / (1ULL * 1024 * 1024 * 1024);
        u64 directory_entry_index = (page >> 21) & (PAGE_DIRECTORY_ENTRIES - 1);
        u64 *directory = page_directories[directory_index];
        u64 *entry = &directory[directory_entry_index];
        u64 directory_entry = *entry;
        u64 *table;

        if ((directory_entry & PAGE_SIZE_FLAG) != 0) {
            table = allocate_table();
            if (table == 0) {
                goto finish;
            }
            u64 physical_base = directory_entry & 0x000FFFFFFFE00000ULL;
            u64 cache_flags =
                directory_entry & (PAGE_WRITE_THROUGH | PAGE_CACHE_DISABLE);
            for (u64 i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
                table[i] = (physical_base + i * PAGE_SIZE) |
                    PAGE_PRESENT_WRITE | cache_flags;
            }
            *entry = (u64)(uintptr_t)table | PAGE_PRESENT_WRITE;
            updated = 1;
        } else {
            table = (u64 *)(uintptr_t)(directory_entry & 0x000FFFFFFFFFF000ULL);
        }

        u64 table_entry_index = (page >> 12) & (PAGE_DIRECTORY_ENTRIES - 1);
        table[table_entry_index] |= PAGE_WRITE_THROUGH | PAGE_CACHE_DISABLE;
        updated = 1;
        if (page == last_page) {
            completed = 1;
            break;
        }
    }

finish:
    if (updated) {
        u64 cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
    }
    return completed;
}
