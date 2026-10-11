#ifndef UEFI_H
#define UEFI_H

#include <stdint.h>

#define EFIAPI __attribute__((ms_abi))
#define EFI_SUCCESS 0
#define EFI_LOAD_ERROR 0x8000000000000001ULL
#define EFI_INVALID_PARAMETER 0x8000000000000002ULL
#define EFI_UNSUPPORTED 0x8000000000000003ULL
#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ULL
#define EFI_NOT_READY 0x8000000000000006ULL
#define EFI_ERROR(status) (((EFI_STATUS)(status) >> 63) != 0)

#define EFI_ALLOCATE_ANY_PAGES 0
#define EFI_ALLOCATE_ADDRESS 2
#define EFI_LOADER_DATA 2
#define EFI_FILE_MODE_READ 1
#define EFI_FILE_READ_ONLY 1
#define EFI_FILE_INFO_ID \
    { 0x09576e92, 0x6d3f, 0x11d2, \
      { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

typedef uint64_t EFI_STATUS;
typedef uintptr_t UINTN;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint16_t CHAR16;
typedef void *EFI_HANDLE;
typedef void *EFI_EVENT;

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} EFI_GUID;

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} EFI_TABLE_HEADER;

typedef struct {
    uint16_t scan_code;
    CHAR16 unicode_char;
} EFI_INPUT_KEY;

typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    void *reset;
    EFI_STATUS (EFIAPI *read_key_stroke)(
        EFI_SIMPLE_TEXT_INPUT_PROTOCOL *this, EFI_INPUT_KEY *key);
    EFI_EVENT wait_for_key;
};

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void *reset;
    EFI_STATUS (EFIAPI *output_string)(
        EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *this, const CHAR16 *string);
    void *test_string;
    void *query_mode;
    void *set_mode;
    EFI_STATUS (EFIAPI *set_attribute)(
        EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *this, UINTN attribute);
    EFI_STATUS (EFIAPI *clear_screen)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *this);
};

typedef struct {
    uint32_t type;
    uint32_t pad;
    EFI_PHYSICAL_ADDRESS physical_start;
    EFI_PHYSICAL_ADDRESS virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

struct EFI_FILE_PROTOCOL {
    uint64_t revision;
    EFI_STATUS (EFIAPI *open)(
        EFI_FILE_PROTOCOL *this, EFI_FILE_PROTOCOL **new_handle,
        const CHAR16 *file_name, uint64_t open_mode, uint64_t attributes);
    EFI_STATUS (EFIAPI *close)(EFI_FILE_PROTOCOL *this);
    void *delete;
    EFI_STATUS (EFIAPI *read)(
        EFI_FILE_PROTOCOL *this, UINTN *buffer_size, void *buffer);
    void *write;
    void *get_position;
    void *set_position;
    EFI_STATUS (EFIAPI *get_info)(
        EFI_FILE_PROTOCOL *this, const EFI_GUID *information_type,
        UINTN *buffer_size, void *buffer);
};

typedef struct {
    uint64_t revision;
    EFI_STATUS (EFIAPI *open_volume)(
        void *this, EFI_FILE_PROTOCOL **root);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct {
    uint64_t size;
    uint64_t file_size;
    uint64_t physical_size;
} EFI_FILE_INFO;

typedef struct {
    uint32_t version;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    uint32_t pixel_format;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;
    uint32_t pixels_per_scan_line;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    uint32_t max_mode;
    uint32_t mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
    UINTN size_of_info;
    EFI_PHYSICAL_ADDRESS framebuffer_base;
    UINTN framebuffer_size;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    void *query_mode;
    void *set_mode;
    void *blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef struct {
    uint32_t revision;
    EFI_HANDLE parent_handle;
    void *system_table;
    EFI_HANDLE device_handle;
} EFI_LOADED_IMAGE_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(
    UINTN type, UINTN memory_type, UINTN pages, EFI_PHYSICAL_ADDRESS *memory);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(
    UINTN *memory_map_size, EFI_MEMORY_DESCRIPTOR *memory_map,
    UINTN *map_key, UINTN *descriptor_size, uint32_t *descriptor_version);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(
    UINTN memory_type, UINTN size, void **buffer);
typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(
    EFI_HANDLE handle, const EFI_GUID *protocol, void **interface);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(
    EFI_HANDLE image_handle, UINTN map_key);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(
    const EFI_GUID *protocol, void *registration, void **interface);

typedef struct {
    EFI_TABLE_HEADER header;
    void *raise_tpl;
    void *restore_tpl;
    EFI_ALLOCATE_PAGES allocate_pages;
    void *free_pages;
    EFI_GET_MEMORY_MAP get_memory_map;
    EFI_ALLOCATE_POOL allocate_pool;
    void *free_pool;
    void *create_event;
    void *set_timer;
    void *wait_for_event;
    void *signal_event;
    void *close_event;
    void *check_event;
    void *install_protocol_interface;
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    EFI_HANDLE_PROTOCOL handle_protocol;
    void *reserved;
    void *register_protocol_notify;
    void *locate_handle;
    void *locate_device_path;
    void *install_configuration_table;
    void *load_image;
    void *start_image;
    void *exit;
    void *unload_image;
    EFI_EXIT_BOOT_SERVICES exit_boot_services;
    void *get_next_monotonic_count;
    void *stall;
    void *set_watchdog_timer;
    void *connect_controller;
    void *disconnect_controller;
    void *open_protocol;
    void *close_protocol;
    void *open_protocol_information;
    void *protocols_per_handle;
    void *locate_handle_buffer;
    EFI_LOCATE_PROTOCOL locate_protocol;
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER header;
    CHAR16 *firmware_vendor;
    uint32_t firmware_revision;
    EFI_HANDLE console_in_handle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *con_in;
    EFI_HANDLE console_out_handle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *con_out;
    EFI_HANDLE standard_error_handle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *std_err;
    void *runtime_services;
    EFI_BOOT_SERVICES *boot_services;
} EFI_SYSTEM_TABLE;

#endif
