#include "shell.h"
#include "vga.h"
#include "keyboard.h"

// Maximum length of a command line
#define LINE_MAX 256

// Buffer to hold the current command line and its length
static char line [LINE_MAX];
static int line_length = 0;

// Function to compare two strings for equality
static int strcmp_(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (u8)*a - (u8)*b; // Return the difference of the first differing characters
}

// Function to compare two strings for equality up to n characters
static int strncmp_(const char *a, const char *b, size_t n) {
    while (n-- && *a && *a == *b) {
        a++;
        b++;
    }
    return (u8)*a - (u8)*b; // Return the difference of the first differing characters or 0 if equal
}

// Function to print the shell prompt
static void print_prompt(void) {
    vga_set_color(0x0a, 0x00);  // Set color to light green on black
    vga_puts("GreenyOS");     // Print the shell prompt
    vga_set_color(0x07, 0x00);  // Reset color to light gray on black
    vga_putc("> ");
}

// Function to handle the 'help' command
static void cmd_help(void) {
    vga_puts("Available commands:\n");
    vga_puts("  help      - Show this help\n");
    vga_puts("  clear     - Clear the screen\n");
    vga_puts("  echo X    - Print X\n");
    vga_puts("  version   - Show kernel version\n");
    vga_puts("  colors    - Color test\n");
    vga_puts("  cpuid     - Show CPU vendor\n");
    vga_puts("  reboot    - Not implemented (safe halt)\n");
    vga_puts("  halt      - Halt the CPU\n");
}

// Function to handle the 'echo' command
static void cmd_echo(const char *args) {
    vga_puts(args);
    vga_putc('\n');
}

// Function to handle the 'version' command
static void cmd_version(void) {
    vga_puts("GreenyOS Kernel Version 0.1\n");
    vga_puts("Built with coffee and love.\n");
}

// Function to handle the 'colors' command
static void cmd_colors(void) {
    for (u8 bg = 0; bg < 16; bg++) {
        for (u8 fg = 0; fg < 16; fg++) {
            vga_set_color(fg, bg);
            vga_putc('A' + (fg % 26));
            vga_putc(' ');
        }
        vga_set_color(0x07, 0x00); // Reset color to light gray on black
        vga_putc('\n');
    }
}

// Function to handle the 'cpuid' command
static void cmd_cpuid(void) {
    u32 eax, ebx, ecx, edx; // Variables to hold CPU information
    char vendor[13];        // Buffer to hold the CPU vendor string
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0)); // Execute the CPUID instruction with EAX=0
    *(u32*)&vendor[0] = ebx; // Copy EBX to the first 4 bytes of vendor
    *(u32*)&vendor[4] = edx; // Copy EDX to the next 4 bytes of vendor
    *(u32*)&vendor[8] = ecx; // Copy ECX to the last 4 bytes of vendor
    vendor[12] = '\0';       // Null-terminate the vendor string
    vga_printf("CPU Vendor: %s\n", vendor); // Print the CPU vendor string

    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1)); // Execute the CPUID instruction with EAX=1
    vga_printf("Family: %u  Model: %u  Stepping: %u\n", (eax >> 8) & 0xF, (eax >> 4) & 0xF, eax & 0xF); // Print the CPU family, model, and stepping information
}

static void cmd_halt(void) {
    vga_puts("System halted. You may safely turn off your computer.\n");
    for (;;) {
        __asm__ volatile("hlt"); // Halt the CPU
    }
}

static void execute(const char* cmd) {
    
    // Skip leading spaces in the command
    while (*cmd == ' ') cmd++;
    
    // Return if the command is empty
    if (*cmd == 0) return;

    if (!strcmp_(cmd, "help"))              cmd_help();         // Execute the 'help' command
    else if (!strcmp_(cmd, "clear"))        vga_clear();        // Execute the 'clear' command
    else if (!strcmp_(cmd, "version"))      cmd_version();      // Execute the 'version' command
    else if (!strcmp_(cmd, "colors"))       cmd_colors();       // Execute the 'colors' command
    else if (!strcmp_(cmd, "cpuid"))        cmd_cpuid();        // Execute the 'cpuid' command
    else if (!strcmp_(cmd, "halt"))         cmd_halt();         // Execute the 'halt' command
    else if (!strcmp_(cmd, "reboot"))       cmd_halt();         // Execute the 'reboot' command (not implemented)
    else if (!strncmp_(cmd, "echo ", 5))    cmd_echo(cmd + 5);  // Execute the 'echo' command with arguments
    else if (!strcmp_(cmd, "echo"))         vga_putc('\n');     // Execute the 'echo' command without arguments
    else {
        vga_set_color(0x0C, 0x00);      // Set color to light red on black for error message
        vga_puts("Unknown command: ");  // Print error message for unknown command
        vga_puts(cmd);                  // Print the unknown command
        vga_putc('\n');                 // Print a newline after the error message
        vga_set_color(0x07, 0x00);      // Reset color to light gray on black
    }
}

void shell_init(void) {
    line_length = 0; // Initialize the command line length to 0
}

void shell_run(void) {
    print_prompt();

    for(;;) {
        u16 c = keyboard_getchar();

        if (c == KEY_ENTER) {
            vga_putc('\n');
            line[line_length] = 0;          // Null-terminate the command line
            execute(line);                  // Execute the command line
            line_length = 0;                // Reset the command line length
            print_prompt();                 // Print the shell prompt again
        } else if (c == KEY_BACKSPACE) {    // Handle backspace key
            if (line_length > 0) {          // If there are characters in the command line
                line_length--;              // Decrease the command line length
                vga_putc('\b');             // Move cursor back
            }
        } else if (c >= 0x100) {
            // special keys (like arrow keys) are ignored for now
        } else if (c >= 0x20 && c < 0x7F) {
            if (line_length < LINE_MAX - 1) {
                line[line_length++] = (char)c;
                vga_putc((char)c);
            }
        }
    }
}