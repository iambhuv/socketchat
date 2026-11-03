// ui.c - Clay layout components plus standard single-line text editing.
// Clay owns layout, raylib owns the platform, and stb_textedit owns editing.
#ifndef UI_C_
#define UI_C_

#include <math.h>
#include <stdlib.h>

#define UI_TITLE    24
#define UI_HEADING  18
#define UI_BODY     15
#define UI_SMALL    13
#define UI_CAPTION  12
#define UI_RADIUS   8
#define UI_CONTROL  40
#define UI_PAD      16
#define UI_TEXT_CAP 2048

typedef struct {
    Color bg, surface, surface2, border, text, muted, accent, accent_hi, accent_soft, ok, warn, err;
} Theme;

static const Theme THEME_LIGHT = {
    .bg = {250, 250, 250, 255}, .surface = {255, 255, 255, 255}, .surface2 = {244, 244, 245, 255},
    .border = {228, 228, 231, 255}, .text = {24, 24, 27, 255}, .muted = {113, 113, 122, 255},
    .accent = {79, 70, 229, 255}, .accent_hi = {67, 56, 202, 255}, .accent_soft = {238, 242, 255, 255},
    .ok = {22, 163, 74, 255}, .warn = {217, 119, 6, 255}, .err = {220, 38, 38, 255},
};

static const Theme THEME_DARK = {
    .bg = {9, 9, 11, 255}, .surface = {24, 24, 27, 255}, .surface2 = {39, 39, 42, 255},
    .border = {63, 63, 70, 255}, .text = {250, 250, 250, 255}, .muted = {161, 161, 170, 255},
    .accent = {99, 102, 241, 255}, .accent_hi = {129, 140, 248, 255}, .accent_soft = {49, 46, 129, 255},
    .ok = {74, 222, 128, 255}, .warn = {251, 191, 36, 255}, .err = {248, 113, 113, 255},
};

typedef struct {
    Font fonts[2];
    bool dark;
    uint64_t active_id, focus_id;
    int cursor;
    void *clay_memory;
} Ui;

static Ui ui;

const Theme *theme(void) { return ui.dark ? &THEME_DARK : &THEME_LIGHT; }

uint64_t ui_id(const char *file, int line, int extra)
{
    uint64_t h = 5381;
    for (const char *p = file; *p; ++p) h = h*33 + (uint8_t)*p;
    h = h*33 + (uint64_t)line;
    h = h*33 + (uint64_t)extra;
    return h ? h : 1;
}

#define UI_ID(extra) ui_id(__FILE__, __LINE__, (extra))

static Clay_Color ui_clay_color(Color c) { return (Clay_Color){ c.r, c.g, c.b, c.a }; }
static Clay_String ui_clay_string(const char *s) { return (Clay_String){ .length = (int32_t)strlen(s), .chars = s }; }

static Clay_ElementId ui_clay_id(const char *kind, uint64_t id)
{
    char key[48];
    int n = snprintf(key, sizeof(key), "%s-%016llx", kind, (unsigned long long)id);
    return Clay_GetElementId((Clay_String){ .length = n, .chars = key });
}

static Clay_Dimensions ui_measure_clay(Clay_StringSlice text, Clay_TextElementConfig *config, void *data)
{
    Ui *state = data;
    Font font = state->fonts[config->fontId < ARRAY_LEN(state->fonts) ? config->fontId : 0];
    char *copy = temp_alloc((size_t)text.length + 1);
    memcpy(copy, text.chars, (size_t)text.length);
    copy[text.length] = '\0';
    Vector2 size = MeasureTextEx(font, copy, (float)config->fontSize, (float)config->letterSpacing);
    return (Clay_Dimensions){ size.x, config->lineHeight ? config->lineHeight : size.y };
}

static void ui_clay_error(Clay_ErrorData error)
{
    nob_log(ERROR, "Clay: %.*s", error.errorText.length, error.errorText.chars);
}

void Clay_Raylib_Render(Clay_RenderCommandArray commands, Font *fonts);

Font ui_load_font(const unsigned char *data, size_t size)
{
    Font font = LoadFontFromMemory(".ttf", data, (int)size, 64, NULL, 0);
    GenTextureMipmaps(&font.texture);
    SetTextureFilter(font.texture, TEXTURE_FILTER_TRILINEAR);
    return font;
}

void ui_init(Font font, Font bold)
{
    ui.fonts[0] = font;
    ui.fonts[1] = bold;
    uint32_t bytes = Clay_MinMemorySize();
    ui.clay_memory = malloc(bytes);
    assert(ui.clay_memory);
    Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(bytes, ui.clay_memory),
                    (Clay_Dimensions){ GetScreenWidth(), GetScreenHeight() },
                    (Clay_ErrorHandler){ .errorHandlerFunction = ui_clay_error });
    Clay_SetMeasureTextFunction(ui_measure_clay, &ui);
}

void ui_shutdown(void)
{
    free(ui.clay_memory);
    ui.clay_memory = NULL;
}

void ui_begin(void)
{
    ui.cursor = MOUSE_CURSOR_DEFAULT;
    Clay_SetLayoutDimensions((Clay_Dimensions){ GetScreenWidth(), GetScreenHeight() });
    Vector2 mouse = GetMousePosition();
    Clay_SetPointerState((Clay_Vector2){ mouse.x, mouse.y }, IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    Vector2 wheel = GetMouseWheelMoveV();
    Clay_UpdateScrollContainers(false, (Clay_Vector2){ wheel.x, wheel.y }, GetFrameTime());
    Clay_BeginLayout();
}

void ui_end(void)
{
    Clay_Raylib_Render(Clay_EndLayout(), ui.fonts);
    SetMouseCursor(ui.cursor);
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) ui.active_id = 0;
}

void ui_text(const char *text, float size, Color color, bool bold)
{
    CLAY_TEXT(ui_clay_string(text), CLAY_TEXT_CONFIG({
        .fontId = bold ? 1 : 0, .fontSize = (uint16_t)size, .textColor = ui_clay_color(color),
    }));
}

void ui_hline(void)
{
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } },
           .backgroundColor = ui_clay_color(theme()->border) }) {}
}

void ui_avatar(const char *name, bool strong)
{
    const Theme *t = theme();
    char initial[2] = { name[0] ? (char)toupper((unsigned char)name[0]) : '?', '\0' };
    CLAY({ .layout = {
               .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40) },
               .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
           }, .backgroundColor = ui_clay_color(strong ? t->accent : t->accent_soft),
           .cornerRadius = CLAY_CORNER_RADIUS(20) }) {
        ui_text(temp_strdup(initial), 18, strong ? WHITE : t->accent, true);
    }
}

static bool ui_hovered(uint64_t id) { return Clay_PointerOver(ui_clay_id("widget", id)); }

static bool ui_click(uint64_t id)
{
    bool over = ui_hovered(id);
    if (over) ui.cursor = MOUSE_CURSOR_POINTING_HAND;
    if (ui.active_id == 0 && over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) ui.active_id = id;
    if (ui.active_id == id && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        ui.active_id = 0;
        return over;
    }
    return false;
}

bool ui_button_id(uint64_t id, const char *label, bool primary)
{
    const Theme *t = theme();
    bool hovered = ui_hovered(id);
    bool clicked = ui_click(id);
    Color fill = primary ? (hovered ? t->accent_hi : t->accent) : (hovered ? t->surface2 : t->surface);
    CLAY({ .id = ui_clay_id("widget", id), .layout = {
               .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(UI_CONTROL) },
               .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
           }, .backgroundColor = ui_clay_color(fill), .cornerRadius = CLAY_CORNER_RADIUS(UI_RADIUS),
           .border = { .color = ui_clay_color(primary ? fill : t->border), .width = CLAY_BORDER_OUTSIDE(1) } }) {
        ui_text(label, UI_BODY, primary ? WHITE : t->text, true);
    }
    return clicked;
}

#define ui_button(label, primary) ui_button_id(UI_ID(0), (label), (primary))

// stb_textedit uses UTF-32 indexes internally, avoiding cursor positions that
// can split UTF-8. The external buffers remain UTF-8 so the rest of the app,
// database, and network protocol do not change.
#define STB_TEXTEDIT_CHARTYPE uint32_t
#include "stb_textedit.h"

typedef struct {
    uint32_t chars[UI_TEXT_CAP];
    size_t count, byte_cap;
    char *bound;
    uint64_t fingerprint;
    float scroll;
    double blink;
    bool initialized;
    STB_TexteditState edit;
} Text_State;

typedef struct { Text_State *state; } Text_Buffer;

// The implementation lives below the adapter callbacks, as stb_textedit
// requires those callbacks to be defined before it is included.
static void stb_textedit_initialize_state(STB_TexteditState *state, int is_single_line);

static uint64_t text_fingerprint(const char *s)
{
    uint64_t h = 1469598103934665603ULL;
    for (; *s; ++s) { h ^= (uint8_t)*s; h *= 1099511628211ULL; }
    return h;
}

static int text_utf8_size(uint32_t cp)
{
    return cp <= 0x7f ? 1 : cp <= 0x7ff ? 2 : cp <= 0xffff ? 3 : 4;
}

static size_t text_bytes(const Text_State *st)
{
    size_t n = 0;
    for (size_t i = 0; i < st->count; ++i) n += text_utf8_size(st->chars[i]);
    return n;
}

static void text_write(Text_State *st)
{
    char *out = st->bound;
    size_t n = 0;
    for (size_t i = 0; i < st->count; ++i) {
        int width;
        const char *encoded = CodepointToUTF8((int)st->chars[i], &width);
        if (n + (size_t)width >= st->byte_cap) break;
        memcpy(out + n, encoded, (size_t)width);
        n += (size_t)width;
    }
    out[n] = '\0';
    st->fingerprint = text_fingerprint(out);
}

static void text_read(Text_State *st, char *buf, size_t cap)
{
    st->bound = buf;
    st->byte_cap = cap;
    st->count = 0;
    for (const char *p = buf; *p && st->count < UI_TEXT_CAP - 1;) {
        int width;
        int cp = GetCodepointNext(p, &width);
        if (width <= 0) break;
        st->chars[st->count++] = cp > 0 ? (uint32_t)cp : '?';
        p += width;
    }
    st->fingerprint = text_fingerprint(buf);
    stb_textedit_initialize_state(&st->edit, true);
    st->edit.cursor = (int)st->count;
    st->initialized = true;
}

static void text_bind(Text_State *st, char *buf, size_t cap)
{
    if (!st->initialized || st->bound != buf || st->byte_cap != cap || st->fingerprint != text_fingerprint(buf)) {
        text_read(st, buf, cap);
    }
}

static int text_string_len(Text_Buffer *buf) { return (int)buf->state->count; }
static uint32_t text_get_char(Text_Buffer *buf, int i) { return buf->state->chars[i]; }
static int text_is_space(uint32_t cp) { return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r'; }

static void text_layout_row(StbTexteditRow *row, Text_Buffer *buf, int start)
{
    Text_State *st = buf->state;
    float width = 0;
    for (size_t i = (size_t)start; i < st->count; ++i) {
        int n;
        const char *s = CodepointToUTF8((int)st->chars[i], &n);
        char glyph[8];
        memcpy(glyph, s, (size_t)n);
        glyph[n] = '\0';
        width += MeasureTextEx(ui.fonts[0], glyph, UI_BODY, 0).x;
    }
    row->x0 = 0; row->x1 = width; row->baseline_y_delta = UI_BODY; row->ymin = 0; row->ymax = UI_BODY;
    row->num_chars = (int)st->count - start;
}

static float text_width(Text_Buffer *buf, int start, int i)
{
    (void)start;
    int n;
    const char *s = CodepointToUTF8((int)buf->state->chars[i], &n);
    char glyph[8];
    memcpy(glyph, s, (size_t)n);
    glyph[n] = '\0';
    return MeasureTextEx(ui.fonts[0], glyph, UI_BODY, 0).x;
}

static void text_delete(Text_Buffer *buf, int at, int count)
{
    Text_State *st = buf->state;
    memmove(st->chars + at, st->chars + at + count, (st->count - (size_t)at - (size_t)count)*sizeof(st->chars[0]));
    st->count -= (size_t)count;
}

static int text_insert_chars(Text_Buffer *buf, int at, const uint32_t *chars, int count)
{
    Text_State *st = buf->state;
    if (count < 0 || st->count + (size_t)count >= UI_TEXT_CAP) return 0;
    size_t bytes = text_bytes(st);
    for (int i = 0; i < count; ++i) bytes += (size_t)text_utf8_size(chars[i]);
    if (bytes >= st->byte_cap) return 0;
    memmove(st->chars + at + count, st->chars + at, (st->count - (size_t)at)*sizeof(st->chars[0]));
    memcpy(st->chars + at, chars, (size_t)count*sizeof(chars[0]));
    st->count += (size_t)count;
    return 1;
}

#define STB_TEXTEDIT_STRING             Text_Buffer
#define STB_TEXTEDIT_STRINGLEN(obj)     text_string_len(obj)
#define STB_TEXTEDIT_LAYOUTROW(row,obj,start) text_layout_row((row), (obj), (start))
#define STB_TEXTEDIT_GETWIDTH(obj,start,i) text_width((obj), (start), (i))
#define STB_TEXTEDIT_KEYTOTEXT(key)     ((key) < 0x10000 ? (key) : -1)
#define STB_TEXTEDIT_GETCHAR(obj,i)     text_get_char((obj), (i))
#define STB_TEXTEDIT_NEWLINE             '\n'
#define STB_TEXTEDIT_DELETECHARS(obj,i,n) text_delete((obj), (i), (n))
#define STB_TEXTEDIT_INSERTCHARS(obj,i,c,n) text_insert_chars((obj), (i), (c), (n))
#define STB_TEXTEDIT_IS_SPACE(ch)       text_is_space(ch)
#define STB_TEXTEDIT_K_SHIFT            0x40000000
#define STB_TEXTEDIT_K_LEFT             0x10001
#define STB_TEXTEDIT_K_RIGHT            0x10002
#define STB_TEXTEDIT_K_UP               0x10003
#define STB_TEXTEDIT_K_DOWN             0x10004
#define STB_TEXTEDIT_K_PGUP             0x10005
#define STB_TEXTEDIT_K_PGDOWN           0x10006
#define STB_TEXTEDIT_K_LINESTART        0x10007
#define STB_TEXTEDIT_K_LINEEND          0x10008
#define STB_TEXTEDIT_K_TEXTSTART        0x10009
#define STB_TEXTEDIT_K_TEXTEND          0x1000a
#define STB_TEXTEDIT_K_DELETE           0x1000b
#define STB_TEXTEDIT_K_BACKSPACE        0x1000c
#define STB_TEXTEDIT_K_UNDO             0x1000d
#define STB_TEXTEDIT_K_REDO             0x1000e
#define STB_TEXTEDIT_K_WORDLEFT         0x1000f
#define STB_TEXTEDIT_K_WORDRIGHT        0x10010
#define STB_TEXTEDIT_IMPLEMENTATION
#include "stb_textedit.h"

static void text_copy_selection(Text_State *st)
{
    int first = st->edit.select_start < st->edit.select_end ? st->edit.select_start : st->edit.select_end;
    int last = st->edit.select_start > st->edit.select_end ? st->edit.select_start : st->edit.select_end;
    if (first == last) return;
    char *out = temp_alloc((size_t)(last - first)*4 + 1);
    size_t n = 0;
    for (int i = first; i < last; ++i) {
        int width;
        const char *s = CodepointToUTF8((int)st->chars[i], &width);
        memcpy(out + n, s, (size_t)width);
        n += (size_t)width;
    }
    out[n] = '\0';
    SetClipboardText(out);
}

static void text_paste(Text_Buffer *buf)
{
    const char *clip = GetClipboardText();
    if (!clip) return;
    uint32_t *chars = temp_alloc((strlen(clip) + 1)*sizeof(*chars));
    int count = 0;
    while (*clip) {
        int width;
        int cp = GetCodepointNext(clip, &width);
        if (width <= 0) break;
        if (cp >= 32 && count < UI_TEXT_CAP - 1) chars[count++] = (uint32_t)cp;
        clip += width;
    }
    stb_textedit_paste(buf, &buf->state->edit, chars, count);
}

static void text_select_word(Text_State *st, bool left)
{
    int edge = st->edit.cursor;
    if (left) {
        while (edge > 0 && text_is_space(st->chars[edge - 1])) --edge;
        while (edge > 0 && !text_is_space(st->chars[edge - 1])) --edge;
        st->edit.select_start = edge;
        st->edit.select_end = st->edit.cursor;
    } else {
        while (edge < (int)st->count && text_is_space(st->chars[edge])) ++edge;
        while (edge < (int)st->count && !text_is_space(st->chars[edge])) ++edge;
        st->edit.select_start = st->edit.cursor;
        st->edit.select_end = edge;
    }
}

#define KEY_REPEAT(k) (IsKeyPressed(k) || IsKeyPressedRepeat(k))

static bool text_edit(Text_State *st)
{
    Text_Buffer buf = { st };
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool control = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool command = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
#ifdef __APPLE__
    (void)control;
    bool shortcut = command;
    bool word = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
#else
    bool shortcut = control;
    bool word = control;
#endif
    int select = shift ? STB_TEXTEDIT_K_SHIFT : 0;
    bool changed = false;

    if (shortcut && IsKeyPressed(KEY_A)) {
        st->edit.select_start = 0;
        st->edit.select_end = st->edit.cursor = (int)st->count;
    } else if (shortcut && IsKeyPressed(KEY_C)) {
        text_copy_selection(st);
    } else if (shortcut && IsKeyPressed(KEY_X)) {
        text_copy_selection(st);
        changed |= stb_textedit_cut(&buf, &st->edit) != 0;
    } else if (shortcut && IsKeyPressed(KEY_V)) {
        text_paste(&buf);
        // Replacing a selection with equally many codepoints still changes
        // the UTF-8 buffer, so do not infer this from the new length.
        changed = true;
    } else if (shortcut && IsKeyPressed(KEY_Z)) {
        stb_textedit_key(&buf, &st->edit, IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? STB_TEXTEDIT_K_REDO : STB_TEXTEDIT_K_UNDO);
        changed = true;
    } else if (shortcut && IsKeyPressed(KEY_Y)) {
        stb_textedit_key(&buf, &st->edit, STB_TEXTEDIT_K_REDO);
        changed = true;
    } else {
        if (KEY_REPEAT(KEY_LEFT) && !command)  stb_textedit_key(&buf, &st->edit, (word ? STB_TEXTEDIT_K_WORDLEFT : STB_TEXTEDIT_K_LEFT) | select);
        if (KEY_REPEAT(KEY_RIGHT) && !command) stb_textedit_key(&buf, &st->edit, (word ? STB_TEXTEDIT_K_WORDRIGHT : STB_TEXTEDIT_K_RIGHT) | select);
        if (KEY_REPEAT(KEY_HOME) || (command && KEY_REPEAT(KEY_LEFT))) stb_textedit_key(&buf, &st->edit, (command ? STB_TEXTEDIT_K_TEXTSTART : STB_TEXTEDIT_K_LINESTART) | select);
        if (KEY_REPEAT(KEY_END) || (command && KEY_REPEAT(KEY_RIGHT))) stb_textedit_key(&buf, &st->edit, (command ? STB_TEXTEDIT_K_TEXTEND : STB_TEXTEDIT_K_LINEEND) | select);
        if (KEY_REPEAT(KEY_BACKSPACE)) {
            if (st->edit.select_start == st->edit.select_end) {
                if (word) text_select_word(st, true);
                else if (command) { st->edit.select_start = 0; st->edit.select_end = st->edit.cursor; }
            }
            size_t before = st->count;
            stb_textedit_key(&buf, &st->edit, STB_TEXTEDIT_K_BACKSPACE);
            changed |= before != st->count;
        }
        if (KEY_REPEAT(KEY_DELETE)) {
            if (st->edit.select_start == st->edit.select_end) {
                if (word) text_select_word(st, false);
                else if (command) { st->edit.select_start = st->edit.cursor; st->edit.select_end = (int)st->count; }
            }
            size_t before = st->count;
            stb_textedit_key(&buf, &st->edit, STB_TEXTEDIT_K_DELETE);
            changed |= before != st->count;
        }
        if (!shortcut) {
            int cp;
            while ((cp = GetCharPressed()) > 0) {
                if (cp >= 32) { stb_textedit_key(&buf, &st->edit, cp); changed = true; }
            }
        }
    }
    if (changed) text_write(st);
    if (changed || KEY_REPEAT(KEY_LEFT) || KEY_REPEAT(KEY_RIGHT) || KEY_REPEAT(KEY_HOME) || KEY_REPEAT(KEY_END)) st->blink = GetTime();
    return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
}

static char *text_slice(const Text_State *st, int start, int end, bool secret)
{
    if (start < 0) start = 0;
    if (end > (int)st->count) end = (int)st->count;
    size_t cap = secret ? (size_t)(end - start) + 1 : (size_t)(end - start)*4 + 1;
    char *out = temp_alloc(cap);
    size_t n = 0;
    for (int i = start; i < end; ++i) {
        if (secret) out[n++] = '*';
        else {
            int width;
            const char *encoded = CodepointToUTF8((int)st->chars[i], &width);
            memcpy(out + n, encoded, (size_t)width);
            n += (size_t)width;
        }
    }
    out[n] = '\0';
    return out;
}

static float text_range_width(const Text_State *st, int start, int end, bool secret)
{
    return MeasureTextEx(ui.fonts[0], text_slice(st, start, end, secret), UI_BODY, 0).x;
}

static void text_caret(Color color)
{
    CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(1), CLAY_SIZING_FIXED(UI_BODY + 3) } },
           .backgroundColor = ui_clay_color(color) }) {}
}

bool ui_input_id(uint64_t id, const char *placeholder, char *buf, size_t cap, Text_State *st, bool secret)
{
    const Theme *t = theme();
    text_bind(st, buf, cap);
    Clay_ElementId clay_id = ui_clay_id("widget", id);
    bool over = Clay_PointerOver(clay_id);
    if (over) ui.cursor = MOUSE_CURSOR_IBEAM;
    Clay_ElementData old = Clay_GetElementData(clay_id);
    Vector2 mouse = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (over) {
            ui.focus_id = id;
            ui.active_id = id;
            Text_Buffer text = { st };
            stb_textedit_click(&text, &st->edit, mouse.x - old.boundingBox.x - 12 + st->scroll, mouse.y - old.boundingBox.y);
            st->blink = GetTime();
        } else if (ui.focus_id == id) ui.focus_id = 0;
    }
    if (ui.active_id == id && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && old.found) {
        Text_Buffer text = { st };
        stb_textedit_drag(&text, &st->edit, mouse.x - old.boundingBox.x - 12 + st->scroll, mouse.y - old.boundingBox.y);
        st->blink = GetTime();
    }
    bool focused = ui.focus_id == id;
    bool submitted = focused && text_edit(st);

    int a = st->edit.select_start < st->edit.select_end ? st->edit.select_start : st->edit.select_end;
    int b = st->edit.select_start > st->edit.select_end ? st->edit.select_start : st->edit.select_end;
    float caret_x = text_range_width(st, 0, st->edit.cursor, secret);
    float total = text_range_width(st, 0, (int)st->count, secret);
    float avail = old.found ? old.boundingBox.width - 24 : 300;
    if (caret_x - st->scroll > avail) st->scroll = caret_x - avail;
    if (caret_x < st->scroll) st->scroll = caret_x;
    float max_scroll = total > avail ? total - avail : 0;
    if (st->scroll > max_scroll) st->scroll = max_scroll;
    if (st->scroll < 0) st->scroll = 0;

    CLAY({ .id = clay_id, .layout = {
               .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(UI_CONTROL) },
               .padding = { .left = 12, .right = 12, .top = 8, .bottom = 8 }, .layoutDirection = CLAY_LEFT_TO_RIGHT,
               .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
           }, .backgroundColor = ui_clay_color(t->surface), .cornerRadius = CLAY_CORNER_RADIUS(UI_RADIUS),
           .border = { .color = ui_clay_color(focused ? t->accent : t->border), .width = CLAY_BORDER_OUTSIDE(1) },
           .clip = { .horizontal = true, .childOffset = { -st->scroll, 0 } } }) {
        if (st->count == 0 && !focused) {
            ui_text(placeholder, UI_BODY, t->muted, false);
        } else if (a == b) {
            ui_text(text_slice(st, 0, st->edit.cursor, secret), UI_BODY, t->text, false);
            if (focused && fmod(GetTime() - st->blink, 1.0) < 0.55) text_caret(t->text);
            ui_text(text_slice(st, st->edit.cursor, (int)st->count, secret), UI_BODY, t->text, false);
        } else {
            ui_text(text_slice(st, 0, a, secret), UI_BODY, t->text, false);
            if (st->edit.cursor == a && focused) text_caret(t->text);
            CLAY({ .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) } },
                   .backgroundColor = ui_clay_color(t->accent_soft) }) {
                ui_text(text_slice(st, a, b, secret), UI_BODY, t->text, false);
            }
            if (st->edit.cursor == b && focused) text_caret(t->text);
            ui_text(text_slice(st, b, (int)st->count, secret), UI_BODY, t->text, false);
        }
    }
    return submitted;
}

#define ui_input(placeholder, buf, st)  ui_input_id(UI_ID(0), (placeholder), (buf), sizeof(buf), (st), false)
#define ui_secret(placeholder, buf, st) ui_input_id(UI_ID(0), (placeholder), (buf), sizeof(buf), (st), true)

// Clay v0.14's official raylib command renderer. It is kept separate from
// the editor: rendering is Clay's output, editing is stb_textedit's state.
#include "clay/clay_renderer_raylib.c"

#endif // UI_C_
