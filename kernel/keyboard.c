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

// Function to handle keyboard interrupts and process scancodes
static void buffer_push(u16 c) {
    u32 next_head = (buffer_head + 1) % BUFFER_SIZE;
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
        // Wait for a character to be available
        __asm__ volatile("hlt");
    }
    u16 c = kbd_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % BUFFER_SIZE;
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
            case 0x53: buffer_push(KEY_DELETE);return;
        }
        return;
    }

    switch (scancode) {
        case 0x2A: case 0x36: shift_pressed = 1; return;
        case 0x3A: caps_lock = !caps_lock; return;
    }

    switch (scancode >= 128) return; // Ignore invalid scancodes

    char c = shift_pressed ? scancode_to_ascii_shift[scancode] : scancode_to_ascii[scancode];
    
    if (caps_lock && c >= 'a' && c <= 'z') c = c - 32; // Convert to uppercase
    
    else if (caps_lock && c >= 'A' && c <= 'Z') c = c + 32; // Convert to lowercase

    if (c) buffer_push((u16)(u8)c);
}  

// Public wrapper called by UEFI
void keyboard_push_char(char c) {
    buffer_push((u16)(u8)c);
}