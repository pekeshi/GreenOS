#ifndef KEYBOARD_H 
#define KEYBOARD_H

// Include the kernel header for type definitions
#include "kernel.h"

// Define key codes for special keys
#define KEY_NONE        0
#define KEY_ENTER       '\n'
#define KEY_BACKSPACE   '\b'
#define KEY_TAB         '\t'
#define KEY_ESCAPE      0x1B
#define KEY_UP          0x100
#define KEY_DOWN        0x101
#define KEY_LEFT        0x102
#define KEY_RIGHT       0x103
#define KEY_F1          0x110
#define KEY_DELETE      0x104

// Prototypes for keyboard functions
void keyboard_init(void);
u16 keyboard_getchar(void);
int keyboard_haschar(void);

#endif // KEYBOARD_H