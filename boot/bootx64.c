#include "uefi.h"
#include "boot_info.h"

#define ELF_PT_LOAD 1
#define ELF_PF_EXECUTE 1
#define ELF_MAX_SIZE (32 * 1024 * 1024)
#define ELF_MAX_PHYSICAL_ADDRESS (16ULL * 1024 * 1024 * 1024)
#define PAGE_SIZE 4096

typedef struct {
    unsigned char magic[4];
    uint8_t class;
    uint8_t data;
    uint8_t ident_version;
    uint8_t os_abi;
    uint8_t abi_version;
    uint8_t padding[7];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_header_offset;
    uint64_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_size;
    uint16_t program_header_count;
    uint16_t section_header_size;
    uint16_t section_header_count;
    uint16_t section_name_index;
} ELF64_HEADER;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} ELF64_PROGRAM_HEADER;

static const EFI_GUID loaded_image_guid =
    { 0x5b1b31a1, 0x9562, 0x11d2, { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static const EFI_GUID simple_file_system_guid =
    { 0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static const EFI_GUID graphics_output_guid =
    { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } };
static const EFI_GUID file_info_guid = EFI_FILE_INFO_ID;

static GreenyOSBootInfo boot_info;
static const CHAR16 *load_error_message;
static EFI_STATUS load_error_status;

static void stop_with_status(EFI_SYSTEM_TABLE *system_table, EFI_STATUS status) {
    static const CHAR16 digits[] = L"0123456789ABCDEF";
    CHAR16 message[] = L"EFI status: 0x0000000000000000\r\n";
    for (unsigned i = 0; i < 16; i++) {
        message[14 + i] = digits[(status >> ((15 - i) * 4)) & 0x0F];
    }
    system_table->con_out->output_string(system_table->con_out, message);
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

static void stop_with_message(EFI_SYSTEM_TABLE *system_table, const CHAR16 *message) {
    system_table->con_out->output_string(system_table->con_out, message);
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

static int load_kernel(
    EFI_SYSTEM_TABLE *system_table,
    EFI_HANDLE image_handle,
    uint64_t *entry_address
) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *file_system = 0;
    EFI_FILE_PROTOCOL *root = 0;
    EFI_FILE_PROTOCOL *kernel_file = 0;
    EFI_FILE_INFO *file_info = 0;
    void *file_buffer = 0;
    UINTN info_size = 512;
    UINTN file_size;
    UINTN read_size;
    EFI_STATUS status;

    status = system_table->boot_services->handle_protocol(
        image_handle, &loaded_image_guid, (void **)&loaded_image);
    if (EFI_ERROR(status) || loaded_image == 0) {
        load_error_message = L"GreenyOS: cannot find loaded-image protocol.\r\n";
        return 0;
    }

    status = system_table->boot_services->handle_protocol(
        loaded_image->device_handle, &simple_file_system_guid, (void **)&file_system);
    if (EFI_ERROR(status) || file_system == 0) {
        load_error_message = L"GreenyOS: cannot find the boot filesystem.\r\n";
        return 0;
    }

    status = file_system->open_volume(file_system, &root);
    if (EFI_ERROR(status) || root == 0) {
        load_error_message = L"GreenyOS: cannot open the boot filesystem.\r\n";
        return 0;
    }

    status = root->open(root, &kernel_file, L"\\KERNEL.ELF",
        EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
    root->close(root);
    if (EFI_ERROR(status) || kernel_file == 0) {
        load_error_message = L"GreenyOS: KERNEL.ELF is missing from the ESP root.\r\n";
        return 0;
    }

    status = system_table->boot_services->allocate_pool(
        EFI_LOADER_DATA, info_size, (void **)&file_info);
    if (EFI_ERROR(status) || file_info == 0) {
        kernel_file->close(kernel_file);
        load_error_message = L"GreenyOS: cannot allocate file metadata buffer.\r\n";
        return 0;
    }

    status = kernel_file->get_info(
        kernel_file, &file_info_guid, &info_size, file_info);
    if (EFI_ERROR(status) || file_info->file_size < sizeof(ELF64_HEADER) ||
        file_info->file_size > ELF_MAX_SIZE) {
        kernel_file->close(kernel_file);
        load_error_message = L"GreenyOS: KERNEL.ELF has an invalid size.\r\n";
        return 0;
    }

    file_size = (UINTN)file_info->file_size;
    status = system_table->boot_services->allocate_pool(
        EFI_LOADER_DATA, file_size, &file_buffer);
    if (EFI_ERROR(status) || file_buffer == 0) {
        kernel_file->close(kernel_file);
        load_error_message = L"GreenyOS: cannot allocate kernel file buffer.\r\n";
        return 0;
    }

    read_size = file_size;
    status = kernel_file->read(kernel_file, &read_size, file_buffer);
    kernel_file->close(kernel_file);
    if (EFI_ERROR(status) || read_size != file_size) {
        load_error_message = L"GreenyOS: cannot read KERNEL.ELF.\r\n";
        return 0;
    }

    ELF64_HEADER *elf = (ELF64_HEADER *)file_buffer;
    if (elf->magic[0] != 0x7f || elf->magic[1] != 'E' ||
        elf->magic[2] != 'L' || elf->magic[3] != 'F' ||
        elf->class != 2 || elf->data != 1 || elf->ident_version != 1 ||
        elf->type != 2 || elf->machine != 62 || elf->version != 1 ||
        elf->header_size != sizeof(ELF64_HEADER) ||
        elf->program_header_size != sizeof(ELF64_PROGRAM_HEADER) ||
        elf->program_header_count == 0 ||
        elf->program_header_offset > file_size ||
        elf->program_header_count >
            (file_size - elf->program_header_offset) / sizeof(ELF64_PROGRAM_HEADER)) {
        load_error_message = L"GreenyOS: KERNEL.ELF has invalid ELF headers.\r\n";
        return 0;
    }

    ELF64_PROGRAM_HEADER *headers =
        (ELF64_PROGRAM_HEADER *)((uint8_t *)file_buffer + elf->program_header_offset);
    uint16_t load_count = 0;
    int entry_in_executable_segment = 0;
    for (uint16_t i = 0; i < elf->program_header_count; i++) {
        ELF64_PROGRAM_HEADER *segment = &headers[i];
        if (segment->type != ELF_PT_LOAD) {
            continue;
        }
        load_count++;
        if (segment->memory_size == 0 ||
            segment->file_size > segment->memory_size ||
            segment->offset > file_size ||
            segment->file_size > file_size - segment->offset ||
            segment->physical_address != segment->virtual_address ||
            segment->physical_address > UINT64_MAX - segment->memory_size ||
            segment->physical_address < PAGE_SIZE ||
            segment->physical_address >= ELF_MAX_PHYSICAL_ADDRESS ||
            segment->memory_size > ELF_MAX_PHYSICAL_ADDRESS - segment->physical_address) {
            load_error_message = L"GreenyOS: KERNEL.ELF has an invalid load segment.\r\n";
            return 0;
        }

        uint64_t segment_end = segment->physical_address + segment->memory_size;
        uint64_t rounded_end = (segment_end + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
        if (rounded_end < segment_end) {
            load_error_message = L"GreenyOS: kernel segment address overflow.\r\n";
            return 0;
        }
        if ((segment->flags & ELF_PF_EXECUTE) != 0 &&
            elf->entry >= segment->physical_address && elf->entry < segment_end) {
            entry_in_executable_segment = 1;
        }

        uint64_t segment_start =
            segment->physical_address & ~(uint64_t)(PAGE_SIZE - 1);
        for (uint16_t j = 0; j < i; j++) {
            ELF64_PROGRAM_HEADER *previous = &headers[j];
            if (previous->type != ELF_PT_LOAD) {
                continue;
            }
            uint64_t previous_start =
                previous->physical_address & ~(uint64_t)(PAGE_SIZE - 1);
            uint64_t previous_end = previous->physical_address + previous->memory_size;
            if (previous_end > UINT64_MAX - (PAGE_SIZE - 1)) {
                load_error_message = L"GreenyOS: kernel segment address overflow.\r\n";
                return 0;
            }
            uint64_t previous_rounded_end =
                (previous_end + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
            if (segment_start < previous_rounded_end && rounded_end > previous_start) {
                load_error_message = L"GreenyOS: kernel load segments overlap.\r\n";
                return 0;
            }
        }

        EFI_PHYSICAL_ADDRESS load_address = segment_start;
        UINTN page_count = (UINTN)((rounded_end - segment_start) / PAGE_SIZE);
        status = system_table->boot_services->allocate_pages(
            EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA, page_count, &load_address);
        if (EFI_ERROR(status) || load_address != segment_start) {
            load_error_message = L"GreenyOS: cannot reserve kernel memory.\r\n";
            load_error_status = status;
            return 0;
        }

        uint8_t *destination = (uint8_t *)segment->physical_address;
        for (uint64_t byte = 0; byte < segment->memory_size; byte++) {
            destination[byte] = 0;
        }
        const uint8_t *source = (const uint8_t *)file_buffer + segment->offset;
        for (uint64_t byte = 0; byte < segment->file_size; byte++) {
            destination[byte] = source[byte];
        }
    }

    if (load_count == 0 || !entry_in_executable_segment) {
        load_error_message = L"GreenyOS: kernel entry is outside executable memory.\r\n";
        return 0;
    }

    *entry_address = elf->entry;
    return 1;
}

static int get_framebuffer(EFI_SYSTEM_TABLE *system_table) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *graphics = 0;
    EFI_STATUS status = system_table->boot_services->locate_protocol(
        &graphics_output_guid, 0, (void **)&graphics);
    if (EFI_ERROR(status) || graphics == 0 || graphics->mode == 0 ||
        graphics->mode->info == 0 || graphics->mode->framebuffer_base == 0) {
        return 0;
    }

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode = graphics->mode->info;
    if (mode->pixel_format > GREENYOS_PIXEL_BIT_MASK ||
        mode->horizontal_resolution == 0 || mode->vertical_resolution == 0 ||
        mode->pixels_per_scan_line < mode->horizontal_resolution ||
        graphics->mode->framebuffer_base >= ELF_MAX_PHYSICAL_ADDRESS ||
        graphics->mode->framebuffer_size >
            ELF_MAX_PHYSICAL_ADDRESS - graphics->mode->framebuffer_base) {
        return 0;
    }

    boot_info.framebuffer.address = graphics->mode->framebuffer_base;
    boot_info.framebuffer.size = graphics->mode->framebuffer_size;
    boot_info.framebuffer.width = mode->horizontal_resolution;
    boot_info.framebuffer.height = mode->vertical_resolution;
    boot_info.framebuffer.pixels_per_scan_line = mode->pixels_per_scan_line;
    boot_info.framebuffer.pixel_format = mode->pixel_format;
    boot_info.framebuffer.red_mask = mode->red_mask;
    boot_info.framebuffer.green_mask = mode->green_mask;
    boot_info.framebuffer.blue_mask = mode->blue_mask;
    boot_info.framebuffer.reserved_mask = mode->reserved_mask;
    return 1;
}

static int exit_boot_services(EFI_SYSTEM_TABLE *system_table, EFI_HANDLE image_handle) {
    const UINTN map_capacity = 1024 * 1024;
    void *memory_map = 0;
    EFI_STATUS status = system_table->boot_services->allocate_pool(
        EFI_LOADER_DATA, map_capacity, &memory_map);
    if (EFI_ERROR(status) || memory_map == 0) {
        return 0;
    }

    for (unsigned attempt = 0; attempt < 4; attempt++) {
        UINTN map_size = map_capacity;
        UINTN map_key = 0;
        UINTN descriptor_size = 0;
        uint32_t descriptor_version = 0;

        status = system_table->boot_services->get_memory_map(
            &map_size, (EFI_MEMORY_DESCRIPTOR *)memory_map, &map_key,
            &descriptor_size, &descriptor_version);
        if (EFI_ERROR(status) || descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR)) {
            return 0;
        }

        boot_info.memory_map = (uint64_t)(uintptr_t)memory_map;
        boot_info.memory_map_size = map_size;
        boot_info.memory_descriptor_size = descriptor_size;
        boot_info.memory_descriptor_version = descriptor_version;

        status = system_table->boot_services->exit_boot_services(image_handle, map_key);
        if (!EFI_ERROR(status)) {
            return 1;
        }
    }
    return 0;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    uint64_t kernel_entry = 0;
    boot_info.version = GREENYOS_BOOT_INFO_VERSION;
    boot_info.size = sizeof(boot_info);

    if (!get_framebuffer(system_table)) {
        stop_with_message(system_table, L"GreenyOS: GOP framebuffer unavailable.\r\n");
    }
    if (!load_kernel(system_table, image_handle, &kernel_entry)) {
        system_table->con_out->output_string(
            system_table->con_out, load_error_message);
        if (load_error_status != EFI_SUCCESS) {
            stop_with_status(system_table, load_error_status);
        }
        stop_with_message(system_table, load_error_message);
    }
    if (!exit_boot_services(system_table, image_handle)) {
        stop_with_message(system_table, L"GreenyOS: ExitBootServices failed.\r\n");
    }

    typedef void (__attribute__((sysv_abi)) *KERNEL_ENTRY)(GreenyOSBootInfo *);
    KERNEL_ENTRY entry = (KERNEL_ENTRY)(uintptr_t)kernel_entry;
    entry(&boot_info);

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
