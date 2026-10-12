#include "include/kernel.h"
#include "include/keyboard.h"
#include "include/vga.h"

#define PCI_CONFIG_ADDRESS 0x0CF8
#define PCI_CONFIG_DATA 0x0CFC
#define XHCI_CLASS 0x0C
#define XHCI_SUBCLASS 0x03
#define XHCI_PROG_IF 0x30
#define XHCI_MAX_PORTS 32
#define XHCI_MAX_SLOTS 8
#define XHCI_RING_TRBS 64
#define XHCI_RING_LINK_INDEX (XHCI_RING_TRBS - 1)
#define XHCI_EVENT_TRBS 256
#define XHCI_TRB_CYCLE 0x01
#define XHCI_TRB_TOGGLE_CYCLE 0x02
#define XHCI_TRB_CHAIN 0x10
#define XHCI_TRB_IOC 0x20
#define XHCI_TRB_IDT 0x40

typedef struct {
    u64 parameter;
    u32 status;
    u32 control;
} XHCI_TRB;

typedef struct {
    volatile XHCI_TRB *trbs;
    u64 physical;
    u32 enqueue;
    u32 cycle;
} XHCI_RING;

typedef struct {
    u64 ring_address;
    u32 ring_size;
    u32 reserved;
} XHCI_ERST_ENTRY;

static volatile u8 *controller;
static volatile u32 *operational;
static volatile u32 *doorbells;
static volatile u32 *runtime;
static volatile XHCI_TRB *event_ring;
static u64 event_ring_physical;
static u32 event_index;
static u32 event_cycle;
static u64 *device_context_array;
static XHCI_RING command_ring;
static XHCI_RING endpoint_ring;
static u8 keyboard_slot;
static u8 keyboard_endpoint;
static volatile u8 keyboard_report[4096] __attribute__((aligned(4096)));
static u8 previous_report[8];
static u16 keyboard_packet_size;
static u8 keyboard_configured;
static u8 caps_lock;
static u32 context_size;
static u32 max_ports;
static u32 max_slots;

static void outl(u16 port, u32 value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static u32 inl(u16 port) {
    u32 value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static u32 pci_read(u8 bus, u8 device, u8 function, u8 offset) {
    u32 address = 0x80000000U | ((u32)bus << 16) |
        ((u32)device << 11) | ((u32)function << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write(u8 bus, u8 device, u8 function, u8 offset, u32 value) {
    u32 address = 0x80000000U | ((u32)bus << 16) |
        ((u32)device << 11) | ((u32)function << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static int pci_memory_bar_size(
    u8 bus, u8 device, u8 function, int is_64_bit, u64 *size_out
) {
    u32 command = pci_read(bus, device, function, 0x04) & 0xFFFF;
    u32 original_low = pci_read(bus, device, function, 0x10);
    u32 original_high = is_64_bit
        ? pci_read(bus, device, function, 0x14)
        : 0;

    pci_write(bus, device, function, 0x04, command & ~2U);
    pci_write(bus, device, function, 0x10, 0xFFFFFFFFU);
    if (is_64_bit) {
        pci_write(bus, device, function, 0x14, 0xFFFFFFFFU);
    }
    u32 mask_low = pci_read(bus, device, function, 0x10);
    u32 mask_high = is_64_bit
        ? pci_read(bus, device, function, 0x14)
        : 0;
    if (is_64_bit) {
        pci_write(bus, device, function, 0x14, original_high);
    }
    pci_write(bus, device, function, 0x10, original_low);
    pci_write(bus, device, function, 0x04, command);

    u64 size;
    if (is_64_bit) {
        u64 address_mask =
            ((u64)mask_high << 32) | (mask_low & 0xFFFFFFF0U);
        size = ~address_mask + 1;
    } else {
        u32 address_mask = mask_low & 0xFFFFFFF0U;
        size = (u32)(~address_mask + 1U);
    }
    if (size == 0 || (size & (size - 1)) != 0) {
        return 0;
    }
    *size_out = size;
    return 1;
}

static int find_controller(u8 *bus_out, u8 *device_out, u8 *function_out) {
    for (u32 bus = 0; bus < 256; bus++) {
        for (u32 device = 0; device < 32; device++) {
            u32 identity = pci_read((u8)bus, (u8)device, 0, 0);
            if ((u16)identity == 0xFFFF) {
                continue;
            }
            u32 header = pci_read((u8)bus, (u8)device, 0, 0x0C);
            u32 function_count = ((header >> 23) & 1) ? 8 : 1;
            for (u32 function = 0; function < function_count; function++) {
                identity = pci_read((u8)bus, (u8)device, (u8)function, 0);
                if ((u16)identity == 0xFFFF) {
                    continue;
                }
                u32 class_info =
                    pci_read((u8)bus, (u8)device, (u8)function, 0x08);
                if (((class_info >> 24) & 0xFF) == XHCI_CLASS &&
                    ((class_info >> 16) & 0xFF) == XHCI_SUBCLASS &&
                    ((class_info >> 8) & 0xFF) == XHCI_PROG_IF) {
                    *bus_out = (u8)bus;
                    *device_out = (u8)device;
                    *function_out = (u8)function;
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void *allocate_dma_page(void) {
    void *page = page_allocate();
    if (page == 0) {
        return 0;
    }
    u64 *words = (u64 *)page;
    for (u32 i = 0; i < 4096 / sizeof(u64); i++) {
        words[i] = 0;
    }
    return page;
}

static int wait_register(volatile u32 *reg, u32 mask, u32 expected) {
    for (u32 attempt = 0; attempt < 10000000; attempt++) {
        if ((*reg & mask) == expected) {
            return 1;
        }
        __asm__ volatile("pause");
    }
    return 0;
}

static void wait_milliseconds(u32 milliseconds) {
    u64 start = timer_ticks_since_boot();
    u64 ticks = (milliseconds + 9) / 10;
    while (timer_ticks_since_boot() - start < ticks) {
        __asm__ volatile("pause");
    }
}

static int initialize_ring(XHCI_RING *ring) {
    ring->trbs = (volatile XHCI_TRB *)allocate_dma_page();
    if (ring->trbs == 0) {
        return 0;
    }
    ring->physical = (u64)(uintptr_t)ring->trbs;
    ring->enqueue = 0;
    ring->cycle = 1;
    ring->trbs[XHCI_RING_LINK_INDEX].parameter = ring->physical;
    ring->trbs[XHCI_RING_LINK_INDEX].status = 0;
    ring->trbs[XHCI_RING_LINK_INDEX].control =
        (6U << 10) | XHCI_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE;
    return 1;
}

static void ring_put(XHCI_RING *ring, XHCI_TRB *trb) {
    if (ring->enqueue == XHCI_RING_LINK_INDEX) {
        ring->trbs[XHCI_RING_LINK_INDEX].control =
            (6U << 10) | XHCI_TRB_TOGGLE_CYCLE | ring->cycle;
        ring->enqueue = 0;
        ring->cycle ^= 1;
    }

    trb->control = (trb->control & ~XHCI_TRB_CYCLE) | ring->cycle;
    ring->trbs[ring->enqueue].parameter = trb->parameter;
    ring->trbs[ring->enqueue].status = trb->status;
    __asm__ volatile("" : : : "memory");
    ring->trbs[ring->enqueue].control = trb->control;
    ring->enqueue++;
}

static int event_pop(XHCI_TRB *event, u32 attempts) {
    while (attempts-- != 0) {
        volatile XHCI_TRB *current = &event_ring[event_index];
        u32 control = current->control;
        if ((control & XHCI_TRB_CYCLE) == event_cycle) {
            event->parameter = current->parameter;
            event->status = current->status;
            event->control = control;

            event_index++;
            if (event_index == XHCI_EVENT_TRBS) {
                event_index = 0;
                event_cycle ^= 1;
            }
            volatile u64 *erdp = (volatile u64 *)(runtime + 0x18 / 4);
            *erdp = (event_ring_physical +
                (u64)event_index * sizeof(XHCI_TRB)) | 8;
            return 1;
        }
        __asm__ volatile("pause");
    }
    return 0;
}

static int wait_completion(u8 type, XHCI_TRB *completion) {
    XHCI_TRB event;
    for (u32 attempt = 0; attempt < 10000000; attempt++) {
        if (!event_pop(&event, 1)) {
            __asm__ volatile("pause");
            continue;
        }
        if (((event.control >> 10) & 0x3F) == type) {
            *completion = event;
            return ((event.status >> 24) & 0xFF) == 1 ? 1 : 0;
        }
    }
    return -1;
}

static int submit_command(XHCI_TRB *command, XHCI_TRB *completion) {
    ring_put(&command_ring, command);
    doorbells[0] = 0;
    int result = wait_completion(33, completion);
    if (result < 0) {
        vga_printf(
            "USB keyboard: xHCI command %u timed out.\n",
            (unsigned int)((command->control >> 10) & 0x3F));
        return 0;
    }
    if (result == 0) {
        vga_printf(
            "USB keyboard: xHCI command %u failed, code %u, slot %u.\n",
            (unsigned int)((command->control >> 10) & 0x3F),
            (unsigned int)((completion->status >> 24) & 0xFF),
            (unsigned int)(completion->control >> 24));
        return 0;
    }
    return 1;
}

static int enable_slot(u8 *slot) {
    XHCI_TRB command = { 0, 0, 9U << 10 };
    XHCI_TRB completion;
    if (!submit_command(&command, &completion)) {
        return 0;
    }
    *slot = (u8)(completion.control >> 24);
    return *slot != 0 && *slot <= max_slots;
}

static void disable_slot(u8 slot) {
    XHCI_TRB command = { 0, 0, (10U << 10) | ((u32)slot << 24) };
    XHCI_TRB completion;
    submit_command(&command, &completion);
}

static int address_device(u8 port, u8 speed, u8 *slot_out) {
    u8 slot;
    if (!enable_slot(&slot)) {
        vga_puts("USB keyboard: Enable Slot failed.\n");
        return 0;
    }

    u64 *device_context = (u64 *)allocate_dma_page();
    u64 *input = (u64 *)allocate_dma_page();
    XHCI_RING ep0;
    if (device_context == 0 || input == 0 || !initialize_ring(&ep0)) {
        vga_puts("USB keyboard: device context allocation failed.\n");
        disable_slot(slot);
        return 0;
    }
    device_context_array[slot] = (u64)(uintptr_t)device_context;
    u32 *input_dwords = (u32 *)input;
    u32 *slot_ctx = input_dwords + context_size / 4;
    u32 *ep0_ctx = slot_ctx + context_size / 4;

    input_dwords[1] = 3;
    slot_ctx[0] = ((u32)speed << 20) | (1U << 27);
    slot_ctx[1] = (u32)port << 16;
    ep0_ctx[1] = (3U << 1) | (4U << 3) |
        ((u32)(speed >= 4 ? 512 : speed == 3 ? 64 : 8) << 16);
    ep0_ctx[2] = (u32)ep0.physical | 1;
    ep0_ctx[3] = (u32)(ep0.physical >> 32);
    ep0_ctx[4] = 8;
    XHCI_TRB command = {
        (u64)(uintptr_t)input,
        0,
        (11U << 10) | (speed >= 4 ? 1U << 9 : 0) |
            ((u32)slot << 24)
    };
    XHCI_TRB completion;
    if (!submit_command(&command, &completion)) {
        vga_printf(
            "USB keyboard: Address Device failed on port %u.\n",
            (unsigned int)port);
        disable_slot(slot);
        return 0;
    }
    endpoint_ring = ep0;
    keyboard_slot = slot;
    keyboard_endpoint = 1;
    *slot_out = slot;
    return 1;
}

static int control_transfer(
    u8 slot, u8 endpoint, u8 request_type, u8 request,
    u16 value, u16 index, void *data, u16 length, u16 *actual_length
) {
    u8 slot_id = slot;
    u8 setup[8] = {
        request_type, request, (u8)value, (u8)(value >> 8),
        (u8)index, (u8)(index >> 8), (u8)length, (u8)(length >> 8)
    };
    XHCI_TRB trb = { 0 };
    for (u32 i = 0; i < sizeof(setup); i++) {
        trb.parameter |= (u64)setup[i] << (i * 8);
    }
    trb.status = 8;
    trb.control = (2U << 10) | XHCI_TRB_IDT | XHCI_TRB_CHAIN |
        (length != 0 ? 3U << 16 : 0);
    ring_put(&endpoint_ring, &trb);

    if (length != 0) {
        trb.parameter = (u64)(uintptr_t)data;
        trb.status = length;
        trb.control = (3U << 10) | XHCI_TRB_CHAIN |
            ((request_type & 0x80) ? 1U << 16 : 0);
        ring_put(&endpoint_ring, &trb);
    }

    trb.parameter = 0;
    trb.status = 0;
    trb.control = (4U << 10) | XHCI_TRB_IOC |
        ((length == 0 || (request_type & 0x80) == 0) ? 1U << 16 : 0);
    ring_put(&endpoint_ring, &trb);

    doorbells[slot_id] = endpoint;
    XHCI_TRB completion;
    if (!event_pop(&completion, 10000000)) {
        vga_puts("USB keyboard: control transfer timed out.\n");
        return 0;
    }
    u32 completion_type = (completion.control >> 10) & 0x3F;
    u32 completion_code = (completion.status >> 24) & 0xFF;
    if (completion_type != 32 || (completion_code != 1 &&
            completion_code != 13) ||
        (u8)(completion.control >> 24) != slot_id) {
        vga_printf(
            "USB keyboard: control transfer completion type %u, code %u.\n",
            completion_type, completion_code);
        return 0;
    }
    if (actual_length != 0) {
        *actual_length =
            (u16)(length - (completion.status & 0x1FFFF));
    }
    return 1;
}

static int set_ep0_packet_size(u8 slot, u16 packet_size) {
    u64 *input = (u64 *)allocate_dma_page();
    if (input == 0) {
        return 0;
    }
    u32 *dwords = (u32 *)input;
    dwords[1] = 1U << 1;
    u32 *ep0_ctx = dwords + context_size / 4 * 2;
    ep0_ctx[1] = (ep0_ctx[1] & 0xFFFFU) | ((u32)packet_size << 16);

    XHCI_TRB command = {
        (u64)(uintptr_t)input,
        0,
        (13U << 10) | ((u32)slot << 24)
    };
    XHCI_TRB completion;
    return submit_command(&command, &completion);
}

static u32 ceil_log2(u32 value) {
    u32 result = 0;
    u32 current = 1;
    while (current < value && result < 15) {
        current <<= 1;
        result++;
    }
    return result;
}

static int configure_keyboard_endpoint(
    u8 slot, u8 port_speed, u8 endpoint_address,
    u8 interval, u8 max_burst_size, u16 packet_size,
    u16 max_esit_payload
) {
    u8 endpoint_number = endpoint_address & 0x0F;
    u8 endpoint_id = (u8)(endpoint_number * 2 + 1);
    if (endpoint_number == 0 || endpoint_id > 31) {
        return 0;
    }

    u64 *input = (u64 *)allocate_dma_page();
    if (input == 0 || !initialize_ring(&endpoint_ring)) {
        return 0;
    }
    u32 *dwords = (u32 *)input;
    dwords[1] = 1U | (1U << endpoint_id);
    u32 *slot_ctx = dwords + context_size / 4;
    u32 *endpoint_ctx = dwords + context_size / 4 * (endpoint_id + 1);
    slot_ctx[0] = (slot_ctx[0] & 0x07FFFFFFU) | ((u32)endpoint_id << 27);

    u32 xhci_interval;
    if (port_speed <= 2) {
        xhci_interval = 3 + ceil_log2(interval == 0 ? 1 : interval);
    } else {
        xhci_interval = interval == 0 ? 0U : (u32)interval - 1;
    }
    if (xhci_interval > 15) {
        xhci_interval = 15;
    }
    endpoint_ctx[0] = (xhci_interval << 16) |
        ((u32)(max_esit_payload & 0xFF) << 24);
    endpoint_ctx[1] = (3U << 1) | (7U << 3) |
        ((u32)max_burst_size << 8) | ((u32)packet_size << 16);
    endpoint_ctx[2] = (u32)endpoint_ring.physical | 1;
    endpoint_ctx[3] = (u32)(endpoint_ring.physical >> 32);
    endpoint_ctx[4] = packet_size |
        ((u32)(max_esit_payload >> 8) << 16);

    XHCI_TRB command = {
        (u64)(uintptr_t)input,
        0,
        (12U << 10) | ((u32)slot << 24)
    };
    XHCI_TRB completion;
    if (!submit_command(&command, &completion)) {
        return 0;
    }
    keyboard_slot = slot;
    keyboard_endpoint = endpoint_id;
    keyboard_packet_size = packet_size;
    return 1;
}

static int configure_hid_keyboard(u8 slot, u8 speed) {
    u8 device_descriptor[18] __attribute__((aligned(16)));
    u16 actual;
    if (!control_transfer(slot, 1, 0x80, 6, 0x0100, 0,
            device_descriptor, sizeof(device_descriptor), &actual) ||
        actual < sizeof(device_descriptor)) {
        vga_puts("USB keyboard: could not read the device descriptor.\n");
        return 0;
    }

    u16 ep0_packet_size;
    if (speed >= 4) {
        if (device_descriptor[7] != 9) {
            vga_puts(
                "USB keyboard: invalid SuperSpeed endpoint-zero packet size.\n");
            return 0;
        }
        ep0_packet_size = 512;
    } else {
        ep0_packet_size = speed == 3 ? 64 : device_descriptor[7];
    }
    if (!set_ep0_packet_size(slot, ep0_packet_size)) {
        vga_puts("USB keyboard: could not update the control endpoint.\n");
        return 0;
    }

    u8 configuration[512] __attribute__((aligned(16)));
    if (!control_transfer(slot, 1, 0x80, 6, 0x0200, 0,
            configuration, sizeof(configuration), &actual) ||
        actual < 9 || configuration[1] != 2) {
        vga_puts("USB keyboard: could not read the configuration descriptor.\n");
        return 0;
    }
    u16 total_length = (u16)(configuration[2] | (configuration[3] << 8));
    if (total_length > actual) {
        total_length = actual;
    }

    u8 configuration_value = configuration[5];
    u8 interface_number = 0;
    u8 endpoint_address = 0;
    u8 interval = 1;
    u8 max_burst_size = 0;
    u16 packet_size = 0;
    u16 max_esit_payload = 0;
    int found_keyboard = 0;
    for (u32 offset = 0; offset + 2 <= total_length;) {
        u8 descriptor_length = configuration[offset];
        u8 descriptor_type = configuration[offset + 1];
        if (descriptor_length < 2 || offset + descriptor_length > total_length) {
            break;
        }
        if (descriptor_type == 4 && descriptor_length >= 9) {
            found_keyboard = configuration[offset + 5] == 3 &&
                configuration[offset + 6] == 1 &&
                configuration[offset + 7] == 1;
            if (found_keyboard) {
                interface_number = configuration[offset + 2];
            }
        } else if (found_keyboard && descriptor_type == 5 &&
            descriptor_length >= 7 &&
            (configuration[offset + 2] & 0x80) != 0 &&
            (configuration[offset + 3] & 3) == 3) {
            endpoint_address = configuration[offset + 2];
            packet_size = (u16)(configuration[offset + 4] |
                ((configuration[offset + 5] & 7) << 8));
            interval = configuration[offset + 6];
            max_esit_payload = packet_size;
            if (speed >= 4) {
                u32 companion_offset = offset + descriptor_length;
                if (companion_offset + 6 > total_length ||
                    configuration[companion_offset + 1] != 48 ||
                    configuration[companion_offset] < 6 ||
                    companion_offset + configuration[companion_offset] >
                        total_length) {
                    vga_puts(
                        "USB keyboard: SuperSpeed endpoint companion descriptor is missing or invalid.\n");
                    return 0;
                }
                max_burst_size = configuration[companion_offset + 2];
                max_esit_payload = (u16)(
                    configuration[companion_offset + 4] |
                    (configuration[companion_offset + 5] << 8));
                if (max_burst_size > 15 ||
                    max_esit_payload < packet_size ||
                    max_esit_payload >
                        (u32)packet_size * (max_burst_size + 1)) {
                    vga_puts(
                        "USB keyboard: invalid SuperSpeed endpoint companion values.\n");
                    return 0;
                }
            }
            break;
        }
        offset += descriptor_length;
    }
    if (!found_keyboard || endpoint_address == 0 || packet_size < 8) {
        vga_puts("USB keyboard: no boot-protocol HID interface was found.\n");
        return 0;
    }

    if (!control_transfer(slot, 1, 0x00, 9, configuration_value, 0,
            0, 0, 0)) {
        vga_puts("USB keyboard: SET_CONFIGURATION failed.\n");
        return 0;
    }
    if (!control_transfer(slot, 1, 0x21, 0x0B, 0, interface_number,
            0, 0, 0)) {
        vga_puts("USB keyboard: SET_PROTOCOL failed.\n");
        return 0;
    }
    control_transfer(slot, 1, 0x21, 0x0A, 0, interface_number, 0, 0, 0);
    if (!configure_keyboard_endpoint(
            slot, speed, endpoint_address, interval, max_burst_size,
            packet_size, max_esit_payload)) {
        vga_puts("USB keyboard: could not configure the interrupt endpoint.\n");
        return 0;
    }
    keyboard_configured = 1;
    return 1;
}

static void submit_keyboard_poll(void) {
    XHCI_TRB trb = {
        (u64)(uintptr_t)keyboard_report,
        keyboard_packet_size,
        (1U << 10) | XHCI_TRB_IOC
    };
    ring_put(&endpoint_ring, &trb);
    doorbells[keyboard_slot] = keyboard_endpoint;
}

static char hid_ascii(u8 usage, int shift, int caps) {
    if (usage >= 4 && usage <= 29) {
        char letter = (char)('a' + usage - 4);
        return shift != caps ? (char)(letter - 'a' + 'A') : letter;
    }
    if (usage >= 30 && usage <= 39) {
        static const char normal[] = "1234567890";
        static const char shifted[] = "!@#$%^&*()";
        return shift ? shifted[usage - 30] : normal[usage - 30];
    }
    switch (usage) {
        case 40: return '\n';
        case 41: return 0x1B;
        case 42: return '\b';
        case 43: return '\t';
        case 44: return ' ';
        case 45: return shift ? '_' : '-';
        case 46: return shift ? '+' : '=';
        case 47: return shift ? '{' : '[';
        case 48: return shift ? '}' : ']';
        case 49: return shift ? '|' : '\\';
        case 51: return shift ? ':' : ';';
        case 52: return shift ? '"' : '\'';
        case 53: return shift ? '~' : '`';
        case 54: return shift ? '<' : ',';
        case 55: return shift ? '>' : '.';
        case 56: return shift ? '?' : '/';
        case 57: return 0;
        default: return 0;
    }
}

static int report_has_key(const volatile u8 *report, u8 usage) {
    for (u32 i = 2; i < 8; i++) {
        if (report[i] == usage) {
            return 1;
        }
    }
    return 0;
}

static void process_keyboard_report(void) {
    if (keyboard_report[2] == 1) {
        return;
    }
    if (report_has_key(keyboard_report, 57) &&
        !report_has_key(previous_report, 57)) {
        caps_lock ^= 1;
    }
    int shift = (keyboard_report[0] & 3) != 0;
    for (u32 i = 2; i < 8; i++) {
        u8 usage = keyboard_report[i];
        if (usage == 0 || report_has_key(previous_report, usage)) {
            continue;
        }
        if (usage >= 73 && usage <= 82) {
            static const u16 special_keys[10] = {
                KEY_DELETE, KEY_UP, KEY_PAGEUP, KEY_DELETE, KEY_DOWN,
                KEY_RIGHT, KEY_LEFT, KEY_DOWN, KEY_UP, KEY_UP
            };
            if (usage == 78) keyboard_push_key(KEY_PAGEDOWN);
            else if (usage == 79) keyboard_push_key(KEY_RIGHT);
            else if (usage == 80) keyboard_push_key(KEY_LEFT);
            else if (usage == 81) keyboard_push_key(KEY_DOWN);
            else if (usage == 82) keyboard_push_key(KEY_UP);
            else keyboard_push_key(special_keys[usage - 73]);
        } else {
            char character = hid_ascii(usage, shift, caps_lock);
            if (character != 0) {
                keyboard_push_key((u16)(u8)character);
            }
        }
    }
    for (u32 i = 0; i < sizeof(previous_report); i++) {
        previous_report[i] = keyboard_report[i];
    }
}

static int initialize_event_ring(void) {
    event_ring = (volatile XHCI_TRB *)allocate_dma_page();
    XHCI_ERST_ENTRY *erst = (XHCI_ERST_ENTRY *)allocate_dma_page();
    if (event_ring == 0 || erst == 0) {
        return 0;
    }
    event_ring_physical = (u64)(uintptr_t)event_ring;
    event_index = 0;
    event_cycle = 1;
    erst[0].ring_address = event_ring_physical;
    erst[0].ring_size = XHCI_EVENT_TRBS;

    volatile u32 *interrupter = runtime + 0x20 / 4;
    interrupter[0] = 0;
    interrupter[2] = 1;
    *(volatile u64 *)(interrupter + 4) = (u64)(uintptr_t)erst;
    *(volatile u64 *)(interrupter + 6) = event_ring_physical | 8;
    return 1;
}

static void handoff_firmware(void) {
    u32 extended_offset = (*(volatile u32 *)(controller + 0x10) >> 16) & 0xFFFF;
    for (u32 count = 0; extended_offset != 0 && count < 128; count++) {
        volatile u32 *capability =
            (volatile u32 *)(controller + (u64)extended_offset * 4);
        u32 header = *capability;
        u32 id = header & 0xFF;
        u32 next = (header >> 8) & 0xFF;
        if (id == 1) {
            capability[0] = header | (1U << 24);
            for (u32 attempt = 0; attempt < 1000000; attempt++) {
                if ((*capability & (1U << 16)) == 0) {
                    return;
                }
                __asm__ volatile("pause");
            }
            return;
        }
        if (next == 0) {
            return;
        }
        extended_offset += next;
    }
}

static int initialize_controller(void) {
    u8 bus, device, function;
    if (!find_controller(&bus, &device, &function)) {
        vga_puts("USB keyboard: no xHCI controller found on PCI.\n");
        return 0;
    }
    vga_printf(
        "USB keyboard: xHCI controller at PCI %u:%u.%u.\n",
        (unsigned int)bus, (unsigned int)device, (unsigned int)function);

    u32 bar_low = pci_read(bus, device, function, 0x10);
    u32 bar_type = bar_low & 0x6;
    if ((bar_low & 1) != 0 || (bar_type != 0 && bar_type != 4)) {
        vga_puts("USB keyboard: xHCI controller has an unsupported BAR.\n");
        return 0;
    }
    u64 bar = bar_low & 0xFFFFFFF0U;
    if (bar_type == 4) {
        bar |= (u64)pci_read(bus, device, function, 0x14) << 32;
    }
    u64 bar_size;
    if (!pci_memory_bar_size(
            bus, device, function, bar_type == 4, &bar_size)) {
        vga_puts("USB keyboard: could not determine the xHCI BAR size.\n");
        return 0;
    }
    if (bar == 0 || bar >= KERNEL_IDENTITY_MAP_SIZE ||
        bar_size > KERNEL_IDENTITY_MAP_SIZE - bar) {
        vga_printf(
            "USB keyboard: xHCI MMIO BAR %p is outside the identity map.\n",
            (void *)(uintptr_t)bar);
        return 0;
    }
    if (!paging_mark_uncached(bar, bar_size)) {
        vga_puts("USB keyboard: could not map the xHCI BAR as uncached.\n");
        return 0;
    }

    u32 command = pci_read(bus, device, function, 0x04) & 0xFFFF;
    pci_write(bus, device, function, 0x04, command | 0x0006U);
    controller = (volatile u8 *)(uintptr_t)bar;
    u8 capability_length = controller[0];
    u32 hcs1 = *(volatile u32 *)(controller + 4);
    u32 hcs2 = *(volatile u32 *)(controller + 8);
    u32 hcc1 = *(volatile u32 *)(controller + 0x10);
    u32 doorbell_offset = *(volatile u32 *)(controller + 0x14) & ~3U;
    u32 runtime_offset = *(volatile u32 *)(controller + 0x18) & ~0x1FU;
    operational = (volatile u32 *)(controller + capability_length);
    doorbells = (volatile u32 *)(controller + doorbell_offset);
    runtime = (volatile u32 *)(controller + runtime_offset);
    max_slots = hcs1 & 0xFF;
    max_ports = (hcs1 >> 24) & 0xFF;
    vga_printf(
        "USB keyboard: xHCI reports %u root ports.\n",
        (unsigned int)max_ports);
    if (max_slots > XHCI_MAX_SLOTS) max_slots = XHCI_MAX_SLOTS;
    if (max_ports > XHCI_MAX_PORTS) max_ports = XHCI_MAX_PORTS;
    context_size = (hcc1 & (1U << 2)) ? 64 : 32;

    handoff_firmware();
    operational[0] &= ~1U;
    if (!wait_register(operational + 1, 1, 1)) {
        vga_puts("USB keyboard: xHCI halt timed out.\n");
        return 0;
    }
    operational[0] |= 2;
    if (!wait_register(operational, 2, 0)) {
        vga_puts("USB keyboard: xHCI reset timed out.\n");
        return 0;
    }
    if (!wait_register(operational + 1, 1U << 11, 0) ||
        (operational[2] & 1) == 0) {
        vga_puts("USB keyboard: xHCI does not support 4 KiB pages.\n");
        return 0;
    }

    device_context_array = (u64 *)allocate_dma_page();
    u64 *scratchpad_array = 0;
    u32 scratchpad_count =
        (((hcs2 >> 21) & 0x1F) << 5) | ((hcs2 >> 27) & 0x1F);
    if (scratchpad_count != 0) {
        scratchpad_array = (u64 *)allocate_dma_page();
        if (scratchpad_array == 0 || scratchpad_count > 256) {
            vga_puts("USB keyboard: unsupported xHCI scratchpad count.\n");
            return 0;
        }
        for (u32 i = 0; i < scratchpad_count; i++) {
            void *scratchpad = allocate_dma_page();
            if (scratchpad == 0) {
                vga_puts("USB keyboard: scratchpad allocation failed.\n");
                return 0;
            }
            scratchpad_array[i] = (u64)(uintptr_t)scratchpad;
        }
        device_context_array[0] = (u64)(uintptr_t)scratchpad_array;
    }
    if (device_context_array == 0) {
        return 0;
    }

    if (!initialize_ring(&command_ring) || !initialize_event_ring()) {
        vga_puts("USB keyboard: xHCI ring allocation failed.\n");
        return 0;
    }
    *(volatile u64 *)(operational + 0x30 / 4) =
        (u64)(uintptr_t)device_context_array;
    *(volatile u64 *)(operational + 0x18 / 4) = command_ring.physical | 1;
    operational[0x38 / 4] = max_slots;
    operational[0] |= 1;
    if (!wait_register(operational + 1, 1, 0)) {
        vga_puts("USB keyboard: xHCI controller failed to start.\n");
        return 0;
    }
    return 1;
}

static int reset_port(u8 port) {
    volatile u32 *port_status = operational + (0x400 / 4) + (port - 1) * 4;
    u32 status = port_status[0];
    if ((status & 1) == 0) {
        return 0;
    }
    u32 speed = (status >> 10) & 0xF;
    int superspeed = speed >= 4;
    u32 reset = superspeed ? 1U << 31 : 1U << 4;
    u32 reset_change = superspeed ? 1U << 19 : 1U << 21;
    port_status[0] = (status & (1U << 9)) | reset_change;
    port_status[0] = (status & (1U << 9)) | reset;
    for (u32 attempt = 0; attempt < 100; attempt++) {
        wait_milliseconds(10);
        status = port_status[0];
        if ((status & reset) == 0 && (status & reset_change) != 0) {
            port_status[0] = (status & (1U << 9)) | reset_change;
            if ((status & 3) != 3 ||
                (superspeed && ((status >> 5) & 0xF) != 0)) {
                return 0;
            }
            return 1;
        }
    }
    vga_printf(
        "USB keyboard: xHCI port reset failed, PORTSC=%p.\n",
        (void *)(uintptr_t)status);
    return 0;
}

int usb_keyboard_init(void) {
    keyboard_configured = 0;
    if (!initialize_controller()) {
        return 0;
    }

    int connected_port_found = 0;
    for (u8 port = 1; port <= max_ports; port++) {
        volatile u32 *port_status =
            operational + (0x400 / 4) + (port - 1) * 4;
        u32 initial_status = port_status[0];
        if ((initial_status & 1) == 0) {
            continue;
        }
        connected_port_found = 1;
        vga_printf(
            "USB keyboard: xHCI port %u connected at speed %u.\n",
            (unsigned int)port,
            (unsigned int)((initial_status >> 10) & 0xF));
        if (!reset_port(port)) {
            continue;
        }
        u8 speed = (u8)((port_status[0] >> 10) & 0xF);
        u8 slot;
        if (!address_device(port, speed, &slot)) {
            vga_puts("USB keyboard: xHCI could not address the device.\n");
            continue;
        }
        if (configure_hid_keyboard(slot, speed)) {
            for (u32 i = 0; i < sizeof(previous_report); i++) {
                previous_report[i] = 0;
            }
            vga_puts("USB keyboard: xHCI HID boot keyboard ready.\n");
            submit_keyboard_poll();
            return 1;
        }
        vga_printf(
            "USB keyboard: setup failed on xHCI port %u.\n",
            (unsigned int)port);
        disable_slot(slot);
    }

    if (!connected_port_found) {
        vga_puts("USB keyboard: no connected devices on xHCI root ports.\n");
    }
    vga_puts("USB keyboard: no supported HID boot keyboard found.\n");
    return 0;
}

void usb_keyboard_poll(void) {
    if (!keyboard_configured) {
        return;
    }
    XHCI_TRB event;
    while (event_pop(&event, 1)) {
        u32 type = (event.control >> 10) & 0x3F;
        if (type == 32 &&
            (u8)(event.control >> 24) == keyboard_slot &&
            ((event.control >> 16) & 0x1F) == keyboard_endpoint) {
            u32 completion_code = (event.status >> 24) & 0xFF;
            if (completion_code == 1 || completion_code == 13) {
                process_keyboard_report();
            }
            submit_keyboard_poll();
        }
    }
}
