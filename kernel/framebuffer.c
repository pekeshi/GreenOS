#include "include/kernel.h"

static GreenyOSFramebuffer screen;
static volatile u32 *pixels;
static int framebuffer_ready;
static u32 cursor_column;
static u32 cursor_row;
static u32 terminal_left;
static u32 terminal_top;
static u32 terminal_columns;
static u32 terminal_rows;
static u32 history_line_count = 1;
static u32 history_scroll_offset;
static u8 terminal_foreground = 7;
static u8 terminal_background;

#define CONSOLE_SCALE 2
#define CONSOLE_CELL_WIDTH (6 * CONSOLE_SCALE)
#define CONSOLE_CELL_HEIGHT (8 * CONSOLE_SCALE)
#define CONSOLE_MAX_COLUMNS 256
#define CONSOLE_HISTORY_LINES 256
#define FRAME_MARGIN 24
#define WINDOW_BORDER 4
#define TITLEBAR_HEIGHT 54
#define WINDOW_INNER_LEFT (FRAME_MARGIN + WINDOW_BORDER)
#define WINDOW_TITLE_TOP (FRAME_MARGIN + WINDOW_BORDER)
#define TERMINAL_TOP (WINDOW_TITLE_TOP + TITLEBAR_HEIGHT)
#define CONSOLE_DEFAULT_THEME 0

typedef struct {
    char character;
    u8 columns[5];
} FONT_GLYPH;

typedef struct {
    const char *name;
    u8 background[3];
    u8 titlebar[3];
    u8 panel[3];
    u8 muted[3];
    u8 foreground[3];
    u8 accents[6][3];
} CONSOLE_THEME;

static const CONSOLE_THEME themes[] = {
    {
        "green",
        { 10, 20, 14 }, { 17, 43, 27 }, { 13, 29, 19 }, { 49, 77, 57 },
        { 224, 246, 227 },
        {
            { 255, 74, 74 }, { 57, 255, 111 }, { 255, 222, 61 },
            { 41, 205, 92 }, { 213, 91, 255 }, { 41, 225, 219 }
        }
    },
    {
        "blue",
        { 10, 16, 29 }, { 17, 31, 57 }, { 13, 22, 40 }, { 48, 65, 93 },
        { 224, 235, 255 },
        {
            { 255, 74, 108 }, { 61, 145, 255 }, { 255, 224, 66 },
            { 60, 111, 255 }, { 207, 93, 255 }, { 50, 225, 239 }
        }
    },
    {
        "red",
        { 27, 11, 14 }, { 56, 19, 25 }, { 39, 15, 19 }, { 91, 49, 54 },
        { 255, 231, 228 },
        {
            { 255, 80, 92 }, { 255, 55, 68 }, { 255, 190, 51 },
            { 224, 48, 65 }, { 215, 83, 255 }, { 48, 221, 208 }
        }
    },
    {
        "orange",
        { 27, 16, 8 }, { 58, 31, 13 }, { 41, 23, 11 }, { 94, 62, 39 },
        { 255, 239, 220 },
        {
            { 255, 82, 60 }, { 255, 139, 41 }, { 255, 225, 56 },
            { 232, 94, 28 }, { 219, 76, 226 }, { 46, 220, 194 }
        }
    },
    {
        "yellow",
        { 25, 22, 8 }, { 53, 46, 13 }, { 39, 34, 10 }, { 87, 77, 38 },
        { 255, 249, 215 },
        {
            { 255, 83, 99 }, { 255, 225, 46 }, { 255, 149, 33 },
            { 227, 191, 20 }, { 214, 86, 255 }, { 43, 224, 199 }
        }
    }
};

static u32 active_theme = CONSOLE_DEFAULT_THEME;
static char terminal_history[CONSOLE_HISTORY_LINES][CONSOLE_MAX_COLUMNS];

static const FONT_GLYPH font[] = {
    { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 'A', { 0x7E, 0x11, 0x11, 0x11, 0x7E } },
    { 'B', { 0x7F, 0x49, 0x49, 0x49, 0x36 } },
    { 'C', { 0x3E, 0x41, 0x41, 0x41, 0x22 } },
    { 'D', { 0x7F, 0x41, 0x41, 0x22, 0x1C } },
    { 'E', { 0x7F, 0x49, 0x49, 0x49, 0x41 } },
    { 'F', { 0x7F, 0x09, 0x09, 0x09, 0x01 } },
    { 'G', { 0x3E, 0x41, 0x49, 0x49, 0x7A } },
    { 'H', { 0x7F, 0x08, 0x08, 0x08, 0x7F } },
    { 'I', { 0x00, 0x41, 0x7F, 0x41, 0x00 } },
    { 'J', { 0x20, 0x40, 0x41, 0x3F, 0x01 } },
    { 'K', { 0x7F, 0x08, 0x14, 0x22, 0x41 } },
    { 'L', { 0x7F, 0x40, 0x40, 0x40, 0x40 } },
    { 'M', { 0x7F, 0x02, 0x0C, 0x02, 0x7F } },
    { 'N', { 0x7F, 0x04, 0x08, 0x10, 0x7F } },
    { 'O', { 0x3E, 0x41, 0x41, 0x41, 0x3E } },
    { 'P', { 0x7F, 0x09, 0x09, 0x09, 0x06 } },
    { 'Q', { 0x3E, 0x41, 0x51, 0x21, 0x5E } },
    { 'R', { 0x7F, 0x09, 0x19, 0x29, 0x46 } },
    { 'S', { 0x46, 0x49, 0x49, 0x49, 0x31 } },
    { 'T', { 0x01, 0x01, 0x7F, 0x01, 0x01 } },
    { 'U', { 0x3F, 0x40, 0x40, 0x40, 0x3F } },
    { 'V', { 0x1F, 0x20, 0x40, 0x20, 0x1F } },
    { 'W', { 0x3F, 0x40, 0x30, 0x40, 0x3F } },
    { 'X', { 0x63, 0x14, 0x08, 0x14, 0x63 } },
    { 'Y', { 0x07, 0x08, 0x70, 0x08, 0x07 } },
    { 'Z', { 0x61, 0x51, 0x49, 0x45, 0x43 } },
    { '0', { 0x3E, 0x51, 0x49, 0x45, 0x3E } },
    { '1', { 0x00, 0x42, 0x7F, 0x40, 0x00 } },
    { '2', { 0x62, 0x51, 0x49, 0x49, 0x46 } },
    { '3', { 0x22, 0x41, 0x49, 0x49, 0x36 } },
    { '4', { 0x18, 0x14, 0x12, 0x7F, 0x10 } },
    { '5', { 0x2F, 0x49, 0x49, 0x49, 0x31 } },
    { '6', { 0x3E, 0x49, 0x49, 0x49, 0x32 } },
    { '7', { 0x01, 0x71, 0x09, 0x05, 0x03 } },
    { '8', { 0x36, 0x49, 0x49, 0x49, 0x36 } },
    { '9', { 0x26, 0x49, 0x49, 0x49, 0x3E } },
    { ':', { 0x00, 0x36, 0x36, 0x00, 0x00 } },
    { '.', { 0x00, 0x60, 0x60, 0x00, 0x00 } },
    { ',', { 0x00, 0x40, 0x20, 0x00, 0x00 } },
    { '-', { 0x08, 0x08, 0x08, 0x08, 0x08 } },
    { '_', { 0x40, 0x40, 0x40, 0x40, 0x40 } },
    { '>', { 0x00, 0x41, 0x22, 0x14, 0x08 } },
    { '<', { 0x08, 0x14, 0x22, 0x41, 0x00 } },
    { '/', { 0x20, 0x10, 0x08, 0x04, 0x02 } },
    { '?', { 0x02, 0x01, 0x51, 0x09, 0x06 } },
    { '!', { 0x00, 0x00, 0x5F, 0x00, 0x00 } },
    { '(', { 0x00, 0x1C, 0x22, 0x41, 0x00 } },
    { ')', { 0x00, 0x41, 0x22, 0x1C, 0x00 } },
    { '=', { 0x14, 0x14, 0x14, 0x14, 0x14 } },
    { '+', { 0x08, 0x08, 0x3E, 0x08, 0x08 } },
    { '\'', { 0x00, 0x05, 0x03, 0x00, 0x00 } },
    { '"', { 0x00, 0x07, 0x00, 0x07, 0x00 } },
    { '[', { 0x00, 0x7F, 0x41, 0x41, 0x00 } },
    { ']', { 0x00, 0x41, 0x41, 0x7F, 0x00 } },
    { '%', { 0x63, 0x13, 0x08, 0x64, 0x63 } },
    { '*', { 0x14, 0x08, 0x3E, 0x08, 0x14 } },
    { '#', { 0x14, 0x7F, 0x14, 0x7F, 0x14 } }
};

void framebuffer_init(const GreenyOSFramebuffer *framebuffer) {
    framebuffer_ready = 0;
    if (framebuffer == 0 || framebuffer->address == 0 ||
        framebuffer->width == 0 || framebuffer->height == 0 ||
        framebuffer->pixels_per_scan_line < framebuffer->width ||
        framebuffer->pixel_format > GREENYOS_PIXEL_BIT_MASK ||
        framebuffer->size < (u64)framebuffer->pixels_per_scan_line *
            framebuffer->height * sizeof(u32)) {
        return;
    }

    screen = *framebuffer;
    pixels = (volatile u32 *)(uintptr_t)screen.address;
    framebuffer_ready = 1;
}

static u32 make_pixel(u8 red, u8 green, u8 blue) {
    if (screen.pixel_format == GREENYOS_PIXEL_RGB_RESERVED_8) {
        return ((u32)blue << 16) | ((u32)green << 8) | red;
    }
    if (screen.pixel_format == GREENYOS_PIXEL_BGR_RESERVED_8) {
        return ((u32)red << 16) | ((u32)green << 8) | blue;
    }

    u32 color = 0;
    const u8 components[3] = { red, green, blue };
    const u32 masks[3] = {
        screen.red_mask, screen.green_mask, screen.blue_mask
    };
    for (u32 component = 0; component < 3; component++) {
        if (masks[component] == 0) {
            continue;
        }
        u32 shift = (u32)__builtin_ctz(masks[component]);
        u32 field = masks[component] >> shift;
        u64 scaled = (u64)components[component] * field / 255;
        color |= ((u32)scaled << shift) & masks[component];
    }
    return color;
}

void framebuffer_fill_rect(
    u32 x, u32 y, u32 width, u32 height,
    u8 red, u8 green, u8 blue
) {
    if (!framebuffer_ready || x >= screen.width || y >= screen.height) {
        return;
    }
    if (width > screen.width - x) {
        width = screen.width - x;
    }
    if (height > screen.height - y) {
        height = screen.height - y;
    }

    u32 pixel = make_pixel(red, green, blue);
    for (u32 row = y; row < y + height; row++) {
        volatile u32 *line = pixels + (u64)row * screen.pixels_per_scan_line + x;
        for (u32 column = 0; column < width; column++) {
            line[column] = pixel;
        }
    }
}

void framebuffer_clear(u8 red, u8 green, u8 blue) {
    if (framebuffer_ready) {
        framebuffer_fill_rect(0, 0, screen.width, screen.height, red, green, blue);
    }
}

static void console_color(u8 index, u8 *red, u8 *green, u8 *blue) {
    const CONSOLE_THEME *theme = &themes[active_theme];
    const u8 *color;
    if (index == 0) {
        color = theme->background;
    } else if (index == 7 || index == 15) {
        color = theme->foreground;
    } else if (index == 8) {
        color = theme->muted;
    } else {
        u8 accent_index = (u8)((index - 1) % 8);
        if (accent_index >= 6) {
            accent_index = 5;
        }
        color = theme->accents[accent_index];
        if (index >= 9) {
            *red = (u8)(((u16)color[0] * 3 + theme->foreground[0]) / 4);
            *green = (u8)(((u16)color[1] * 3 + theme->foreground[1]) / 4);
            *blue = (u8)(((u16)color[2] * 3 + theme->foreground[2]) / 4);
            return;
        }
    }
    *red = color[0];
    *green = color[1];
    *blue = color[2];
}

static void console_fill_rect(
    u32 x, u32 y, u32 width, u32 height, u8 color_index
) {
    u8 red, green, blue;
    console_color(color_index, &red, &green, &blue);
    framebuffer_fill_rect(x, y, width, height, red, green, blue);
}

static void console_draw_glyph_at(char character, u32 x, u32 y, u8 foreground) {
    char upper = character;
    if (upper >= 'a' && upper <= 'z') {
        upper = (char)(upper - 'a' + 'A');
    }

    const u8 *glyph = font[0].columns;
    for (u32 i = 0; i < sizeof(font) / sizeof(font[0]); i++) {
        if (font[i].character == upper) {
            glyph = font[i].columns;
            break;
        }
    }

    u8 red, green, blue;
    console_color(foreground, &red, &green, &blue);
    for (u32 glyph_x = 0; glyph_x < 5; glyph_x++) {
        for (u32 glyph_y = 0; glyph_y < 7; glyph_y++) {
            if ((glyph[glyph_x] & (1U << glyph_y)) != 0) {
                framebuffer_fill_rect(
                    x + glyph_x * CONSOLE_SCALE,
                    y + glyph_y * CONSOLE_SCALE,
                    CONSOLE_SCALE, CONSOLE_SCALE, red, green, blue);
            }
        }
    }
}

static void console_draw_glyph(char character, u32 column, u32 row) {
    u32 x = terminal_left + column * CONSOLE_CELL_WIDTH;
    u32 y = terminal_top + row * CONSOLE_CELL_HEIGHT;
    console_fill_rect(x, y, CONSOLE_CELL_WIDTH, CONSOLE_CELL_HEIGHT,
        terminal_background);
    console_draw_glyph_at(character, x, y, terminal_foreground);
}

static void console_draw_cursor(void) {
    u32 x = terminal_left + cursor_column * CONSOLE_CELL_WIDTH;
    u32 y = terminal_top + cursor_row * CONSOLE_CELL_HEIGHT
        + CONSOLE_CELL_HEIGHT - 2;
    u8 red, green, blue;
    console_color(10, &red, &green, &blue);
    framebuffer_fill_rect(x, y, CONSOLE_CELL_WIDTH - 2, 2, red, green, blue);
}

static u32 console_history_start(void) {
    u32 start = history_line_count > terminal_rows
        ? history_line_count - terminal_rows
        : 0;
    u32 oldest = history_line_count > CONSOLE_HISTORY_LINES
        ? history_line_count - CONSOLE_HISTORY_LINES
        : 0;
    if (start < oldest) {
        start = oldest;
    }
    if (history_scroll_offset > start - oldest) {
        return oldest;
    }
    return start - history_scroll_offset;
}

static void console_render_history(void) {
    if (terminal_rows == 0 || terminal_columns == 0) {
        return;
    }

    console_fill_rect(
        terminal_left, terminal_top,
        screen.width - terminal_left - (FRAME_MARGIN + WINDOW_BORDER),
        terminal_rows * CONSOLE_CELL_HEIGHT, terminal_background);

    u32 start = console_history_start();
    u32 oldest = history_line_count > CONSOLE_HISTORY_LINES
        ? history_line_count - CONSOLE_HISTORY_LINES
        : 0;
    for (u32 row = 0; row < terminal_rows; row++) {
        u32 line = start + row;
        if (line >= history_line_count) {
            break;
        }
        if (line < oldest) {
            continue;
        }
        for (u32 column = 0; column < terminal_columns; column++) {
            char character =
                terminal_history[line % CONSOLE_HISTORY_LINES][column];
            if (character != 0 && character != ' ') {
                console_draw_glyph(character, column, row);
            }
        }
    }

    if (history_scroll_offset == 0 && history_line_count > start) {
        cursor_row = history_line_count - 1 - start;
        console_draw_cursor();
    }
}

static void console_scroll(void) {
    u32 copy_width =
        screen.width - terminal_left - (FRAME_MARGIN + WINDOW_BORDER);
    u32 copy_height = (terminal_rows - 1) * CONSOLE_CELL_HEIGHT;
    u32 *destination = (u32 *)(uintptr_t)screen.address;
    for (u32 y = 0; y < copy_height; y++) {
        volatile u32 *dst = (volatile u32 *)(
            destination + (u64)(terminal_top + y)
            * screen.pixels_per_scan_line
            + terminal_left);
        volatile u32 *src = (volatile u32 *)(
            destination + (u64)(terminal_top + CONSOLE_CELL_HEIGHT + y)
            * screen.pixels_per_scan_line + terminal_left);
        u32 pixels_to_copy = copy_width;
        __asm__ volatile(
            "cld; rep movsl"
            : "+D"(dst), "+S"(src), "+c"(pixels_to_copy)
            :
            : "memory");
    }
    console_fill_rect(
        terminal_left, terminal_top + copy_height,
        copy_width, CONSOLE_CELL_HEIGHT, terminal_background);
}

static void console_redraw(void) {
    if (!framebuffer_ready || terminal_columns == 0 || terminal_rows == 0) {
        return;
    }

    const CONSOLE_THEME *theme = &themes[active_theme];
    framebuffer_fill_rect(
        0, 0, screen.width, screen.height,
        theme->background[0], theme->background[1], theme->background[2]);
    framebuffer_fill_rect(
        FRAME_MARGIN, FRAME_MARGIN,
        screen.width - 2 * FRAME_MARGIN,
        screen.height - 2 * FRAME_MARGIN,
        theme->muted[0], theme->muted[1], theme->muted[2]);
    framebuffer_fill_rect(
        WINDOW_INNER_LEFT, WINDOW_TITLE_TOP,
        screen.width - 2 * (FRAME_MARGIN + WINDOW_BORDER),
        screen.height - 2 * (FRAME_MARGIN + WINDOW_BORDER),
        theme->panel[0], theme->panel[1], theme->panel[2]);
    framebuffer_fill_rect(
        WINDOW_INNER_LEFT, WINDOW_TITLE_TOP,
        screen.width - 2 * (FRAME_MARGIN + WINDOW_BORDER),
        TITLEBAR_HEIGHT,
        theme->titlebar[0], theme->titlebar[1], theme->titlebar[2]);
    static const char title[] = "GREENYOS - " GREENYOS_VERSION " - PEKESHI";
    u32 title_length = 0;
    while (title[title_length] != 0) {
        title_length++;
    }
    u32 title_width = title_length * CONSOLE_CELL_WIDTH;
    u32 titlebar_width = screen.width - 2 * (FRAME_MARGIN + WINDOW_BORDER);
    u32 title_x = WINDOW_INNER_LEFT
        + (title_width < titlebar_width ? (titlebar_width - title_width) / 2 : 0);
    u32 title_y = WINDOW_TITLE_TOP + (TITLEBAR_HEIGHT - CONSOLE_CELL_HEIGHT) / 2;
    for (u32 i = 0; i < title_length; i++) {
        console_draw_glyph_at(
            title[i], title_x + i * CONSOLE_CELL_WIDTH, title_y, 7);
    }
    console_render_history();
}

void framebuffer_console_init(void) {
    if (!framebuffer_ready ||
        screen.width <= 2 * (FRAME_MARGIN + WINDOW_BORDER) ||
        screen.height <= TERMINAL_TOP + FRAME_MARGIN + WINDOW_BORDER +
            CONSOLE_CELL_HEIGHT) {
        return;
    }

    u32 terminal_width = screen.width - 2 * (FRAME_MARGIN + WINDOW_BORDER);
    u32 terminal_height = screen.height - TERMINAL_TOP - FRAME_MARGIN
        - WINDOW_BORDER;
    terminal_columns = terminal_width / CONSOLE_CELL_WIDTH;
    if (terminal_columns > CONSOLE_MAX_COLUMNS) {
        terminal_columns = CONSOLE_MAX_COLUMNS;
    }
    terminal_rows = terminal_height / CONSOLE_CELL_HEIGHT;
    terminal_left = WINDOW_INNER_LEFT;
    terminal_top = TERMINAL_TOP;
    active_theme = CONSOLE_DEFAULT_THEME;
    cursor_column = 0;
    cursor_row = 0;
    history_line_count = 1;
    history_scroll_offset = 0;
    for (u32 line = 0; line < CONSOLE_HISTORY_LINES; line++) {
        for (u32 column = 0; column < CONSOLE_MAX_COLUMNS; column++) {
            terminal_history[line][column] = 0;
        }
    }
    terminal_foreground = 7;
    terminal_background = 0;
    console_redraw();
}

u32 framebuffer_console_theme_count(void) {
    return sizeof(themes) / sizeof(themes[0]);
}

const char *framebuffer_console_theme_name(u32 index) {
    if (index >= framebuffer_console_theme_count()) {
        return 0;
    }
    return themes[index].name;
}

int framebuffer_console_set_theme(const char *name) {
    if (name == 0) {
        return 0;
    }

    for (u32 index = 0; index < framebuffer_console_theme_count(); index++) {
        const char *candidate = themes[index].name;
        const char *requested = name;
        while (*candidate != 0 && *candidate == *requested) {
            candidate++;
            requested++;
        }
        if (*candidate == 0 && *requested == 0) {
            active_theme = index;
            console_redraw();
            return 1;
        }
    }
    return 0;
}

void framebuffer_console_clear(void) {
    if (!framebuffer_ready || terminal_columns == 0 || terminal_rows == 0) {
        return;
    }
    cursor_column = 0;
    cursor_row = 0;
    history_line_count = 1;
    history_scroll_offset = 0;
    for (u32 line = 0; line < CONSOLE_HISTORY_LINES; line++) {
        for (u32 column = 0; column < CONSOLE_MAX_COLUMNS; column++) {
            terminal_history[line][column] = 0;
        }
    }
    console_render_history();
}

void framebuffer_console_scroll_page(int direction) {
    if (!framebuffer_ready || terminal_rows == 0 || direction == 0) {
        return;
    }

    u32 oldest = history_line_count > CONSOLE_HISTORY_LINES
        ? history_line_count - CONSOLE_HISTORY_LINES
        : 0;
    u32 live_start = history_line_count > terminal_rows
        ? history_line_count - terminal_rows
        : 0;
    u32 maximum_offset = live_start - oldest;

    if (direction > 0) {
        u32 amount = terminal_rows;
        history_scroll_offset = amount > maximum_offset - history_scroll_offset
            ? maximum_offset
            : history_scroll_offset + amount;
    } else {
        u32 amount = terminal_rows;
        history_scroll_offset = amount > history_scroll_offset
            ? 0
            : history_scroll_offset - amount;
    }
    console_render_history();
}

void framebuffer_console_set_color(u8 foreground, u8 background) {
    terminal_foreground = foreground & 0x0F;
    terminal_background = background & 0x0F;
}

void framebuffer_console_putc(char character) {
    if (!framebuffer_ready || terminal_columns == 0 || terminal_rows == 0) {
        return;
    }

    if (history_scroll_offset == 0) {
        u32 old_x = terminal_left + cursor_column * CONSOLE_CELL_WIDTH;
        u32 old_y = terminal_top + cursor_row * CONSOLE_CELL_HEIGHT
            + CONSOLE_CELL_HEIGHT - 2;
        console_fill_rect(old_x, old_y, CONSOLE_CELL_WIDTH - 2, 2,
            terminal_background);
    }

    if (character == '\r') {
        cursor_column = 0;
    } else if (character == '\n') {
        cursor_column = 0;
        history_line_count++;
        for (u32 column = 0; column < CONSOLE_MAX_COLUMNS; column++) {
            terminal_history[(history_line_count - 1)
                % CONSOLE_HISTORY_LINES][column] = 0;
        }
        if (history_scroll_offset != 0) {
            console_render_history();
            return;
        }
        if (history_line_count > terminal_rows) {
            console_scroll();
            cursor_row = terminal_rows - 1;
        } else {
            cursor_row = history_line_count - 1;
        }
    } else if (character == '\b') {
        if (cursor_column > 0) {
            cursor_column--;
            terminal_history[(history_line_count - 1)
                % CONSOLE_HISTORY_LINES][cursor_column] = ' ';
            if (history_scroll_offset == 0) {
                console_draw_glyph(' ', cursor_column, cursor_row);
            }
        }
    } else if (character == '\t') {
        u32 spaces = 4 - (cursor_column & 3);
        while (spaces-- != 0) {
            framebuffer_console_putc(' ');
        }
        return;
    } else if (character >= 0x20 && character <= 0x7E) {
        terminal_history[(history_line_count - 1)
            % CONSOLE_HISTORY_LINES][cursor_column] = character;
        if (history_scroll_offset == 0) {
            console_draw_glyph(character, cursor_column, cursor_row);
        }
        cursor_column++;
    }

    if (cursor_column >= terminal_columns) {
        cursor_column = 0;
        history_line_count++;
        for (u32 column = 0; column < CONSOLE_MAX_COLUMNS; column++) {
            terminal_history[(history_line_count - 1)
                % CONSOLE_HISTORY_LINES][column] = 0;
        }
        if (history_scroll_offset == 0 && history_line_count > terminal_rows) {
            console_scroll();
            cursor_row = terminal_rows - 1;
        } else if (history_scroll_offset == 0) {
            cursor_row = history_line_count - 1;
        } else {
            console_render_history();
            return;
        }
    }
    if (history_scroll_offset == 0) {
        console_draw_cursor();
    }
}
