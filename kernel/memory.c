#include "include/kernel.h"

#define PAGE_SIZE 4096ULL
#define MAX_PHYSICAL_MEMORY (16ULL * 1024 * 1024 * 1024)
#define PAGE_COUNT (MAX_PHYSICAL_MEMORY / PAGE_SIZE)
#define BITMAP_SIZE (PAGE_COUNT / 8)
#define EFI_CONVENTIONAL_MEMORY 7

static u8 page_bitmap[BITMAP_SIZE];
static u64 search_start;
static u64 free_page_count;

static void set_page_used(u64 page) {
    page_bitmap[page >> 3] |= (u8)(1U << (page & 7));
}

static void set_page_free(u64 page) {
    page_bitmap[page >> 3] &= (u8)~(1U << (page & 7));
}

static int page_is_free(u64 page) {
    return (page_bitmap[page >> 3] & (1U << (page & 7))) == 0;
}

void memory_init(const GreenyOSBootInfo *boot_info) {
    for (u64 i = 0; i < BITMAP_SIZE; i++) {
        page_bitmap[i] = 0xFF;
    }

    if (boot_info == 0 || boot_info->memory_descriptor_size < 40 ||
        boot_info->memory_descriptor_size > boot_info->memory_map_size) {
        return;
    }

    const u8 *map = (const u8 *)(uintptr_t)boot_info->memory_map;
    u64 descriptor_count =
        boot_info->memory_map_size / boot_info->memory_descriptor_size;

    for (u64 i = 0; i < descriptor_count; i++) {
        const u8 *descriptor = map + i * boot_info->memory_descriptor_size;
        uint32_t type = *(const uint32_t *)descriptor;
        uint64_t physical_start = *(const uint64_t *)(descriptor + 8);
        uint64_t number_of_pages = *(const uint64_t *)(descriptor + 24);
        if (type != EFI_CONVENTIONAL_MEMORY ||
            physical_start >= MAX_PHYSICAL_MEMORY) {
            continue;
        }

        u64 first_page = (physical_start + PAGE_SIZE - 1) / PAGE_SIZE;
        u64 last_page = first_page + number_of_pages;
        if (last_page < first_page || last_page > PAGE_COUNT) {
            last_page = PAGE_COUNT;
        }
        if (first_page >= last_page) {
            continue;
        }

        for (u64 page = first_page; page < last_page; page++) {
            if (page_is_free(page)) {
                continue;
            }
            set_page_free(page);
            free_page_count++;
        }
    }

    if (page_is_free(0)) {
        set_page_used(0);
        free_page_count--;
    }
    search_start = 1;
}

void *page_allocate(void) {
    if (free_page_count == 0) {
        return 0;
    }

    for (u64 pass = 0; pass < 2; pass++) {
        u64 start = pass == 0 ? search_start : 1;
        u64 end = pass == 0 ? PAGE_COUNT : search_start;
        for (u64 page = start; page < end; page++) {
            if (!page_is_free(page)) {
                continue;
            }
            set_page_used(page);
            free_page_count--;
            search_start = page + 1;
            if (search_start >= PAGE_COUNT) {
                search_start = 1;
            }
            return (void *)(uintptr_t)(page * PAGE_SIZE);
        }
    }
    return 0;
}

void page_free(void *address) {
    u64 physical = (u64)(uintptr_t)address;
    if (physical == 0 || physical >= MAX_PHYSICAL_MEMORY ||
        (physical & (PAGE_SIZE - 1)) != 0) {
        return;
    }

    u64 page = physical / PAGE_SIZE;
    if (!page_is_free(page)) {
        set_page_free(page);
        free_page_count++;
        if (page < search_start) {
            search_start = page;
        }
    }
}

u64 memory_free_pages(void) {
    return free_page_count;
}
