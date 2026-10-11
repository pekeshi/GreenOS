#include "keyboard.h"

// Define the keyboard buffer
#define KEYBOARD_BUFFER_SIZE 256

// Keyboard buffer and indices
static volatile u16 kbd_buffer[KEYBOARD_BUFFER_SIZE];
static volatile u32 buffer_head = 0;
static volatile u32 buffer_tail = 0;

// Scancode to ASCII mapping for US keyboard layout
static const char scancode_to_ascii[128] = {
    0,   27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',
    0,'*', 0,  ' ',
};

// US keyboard scancode set 1 with shift key pressed
static const char scancode_to_ascii_shift[128] = {
    0,   27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,   'A','S','D','F','G','H','J','K','L',':','"','~',
    0,   '|','Z','X','C','V','B','N','M','<','>','?',
    0,   '*', 0,  ' ',
};

// State variables for modifier keys
static int shift_pressed    = 0;
static int caps_lock        = 0;
static int extended         = 0;

static u8 inb(u16 port) {
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void outb(u16 port, u8 value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static int wait_for_controller(u8 mask, u8 expected) {
    for (u32 attempt = 0; attempt < 1000000; attempt++) {
        if ((inb(0x64) & mask) == expected) {
            return 1;
        }
        __asm__ volatile("pause");
    }
    return 0;
}

int keyboard_controller_init(void) {
    if (!wait_for_controller(0x02, 0)) {
        return 0;
    }
    outb(0x64, 0x20);
    if (!wait_for_controller(0x01, 0x01)) {
        return 0;
    }

    u8 configuration = inb(0x60);
    configuration |= 0x41;
    configuration &= (u8)~0x10;

    if (!wait_for_controller(0x02, 0)) {
        return 0;
    }
    outb(0x64, 0x60);
    if (!wait_for_controller(0x02, 0)) {
        return 0;
    }
    outb(0x60, configuration);
    if (!wait_for_controller(0x02, 0)) {
        return 0;
    }
    outb(0x64, 0xAE);
    return wait_for_controller(0x02, 0);
}


// Function to handle keyboard interrupts and process scancodes
static void buffer_push(u16 c) {
    u32 next_head = (buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
    if (next_head != buffer_tail) {
        kbd_buffer[buffer_head] = c;
        buffer_head = next_head;
    }
}

int keyboard_haschar(void) {
    return buffer_head != buffer_tail;
}

u16 keyboard_getchar(void) {
    while (buffer_head == buffer_tail) {
        keyboard_wait_for_input();
    }
    u16 c = kbd_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

void keyboard_init(void) {
    buffer_head = 0;
    buffer_tail = 0;
    shift_pressed = 0;
    caps_lock = 0;
    extended = 0;
}

void keyboard_inject_scancode(u8 scancode) {
    
    if (scancode == 0xE0) { extended = 1; return; }
    
    if (scancode & 0x80) { // key release
        u8 code = scancode & 0x7F;
        if (code == 0x2A || code == 0x36) shift_pressed = 0;
        extended = 0;
        return;
    }

    if (extended) {
        extended = 0;
        switch (scancode) {
            case 0x48: buffer_push(KEY_UP);    return;
            case 0x50: buffer_push(KEY_DOWN);  return;
            case 0x4B: buffer_push(KEY_LEFT);  return;
            case 0x4D: buffer_push(KEY_RIGHT); return;
            case 0x49: buffer_push(KEY_PAGEUP); return;
            case 0x51: buffer_push(KEY_PAGEDOWN); return;
            case 0x53: buffer_push(KEY_DELETE);return;
        }
        return;
    }

    switch (scancode) {
        case 0x2A: case 0x36: shift_pressed = 1; return;
        case 0x3A: caps_lock = !caps_lock; return;
    }

    if (scancode >= sizeof(scancode_to_ascii)) return;

    char c = shift_pressed ? scancode_to_ascii_shift[scancode] : scancode_to_ascii[scancode];
    
    if (caps_lock && c >= 'a' && c <= 'z') c = c - 32; // Convert to uppercase
    
    else if (caps_lock && c >= 'A' && c <= 'Z') c = c + 32; // Convert to lowercase

    if (c) buffer_push((u16)(u8)c);
}  

void keyboard_push_char(char c) {
    keyboard_push_key((u16)(u8)c);
}

void keyboard_push_key(u16 key) {
    buffer_push(key);
}

void keyboard_irq_handler(void) {
    if ((inb(0x64) & 0x01) != 0) {
        keyboard_inject_scancode(inb(0x60));
    }
    outb(0x20, 0x20);
}