#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include <stdint.h>

#define GREENYOS_BOOT_INFO_VERSION 1

enum {
    GREENYOS_PIXEL_RGB_RESERVED_8 = 0,
    GREENYOS_PIXEL_BGR_RESERVED_8 = 1,
    GREENYOS_PIXEL_BIT_MASK = 2
};

typedef struct {
    uint64_t address;
    uint64_t size;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scan_line;
    uint32_t pixel_format;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;
} GreenyOSFramebuffer;

typedef struct {
    uint32_t version;
    uint32_t size;
    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_descriptor_size;
    uint32_t memory_descriptor_version;
    uint32_t reserved;
    GreenyOSFramebuffer framebuffer;
} GreenyOSBootInfo;

#endif
