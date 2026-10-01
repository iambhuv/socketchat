// ui.c - the few widgets the client needs: panel, button, text input,
// wrapped text, avatar. Immediate mode: every widget is a function you call
// each frame, and it tells you what happened.
//
// Design system, so every screen looks like the same app:
//   type scale   title 24  heading 18  body 15  small 13  caption 12
//   spacing      multiples of 8
//   radius       8 for controls, 12 for cards and bubbles
//   surfaces     flat, 1px border, no shadows
//   colors       one neutral ramp, one accent, three status colors
//
// A widget is identified by the source line that calls it (UI_ID). The
// toolkit's own bookkeeping lives in one `ui` struct below. Application
// state does not belong here.
#ifndef UI_C_
#define UI_C_

#include <math.h>

#define UI_TITLE    24
#define UI_HEADING  18
#define UI_BODY     15
#define UI_SMALL    13
#define UI_CAPTION  12
#define UI_RADIUS   8
#define UI_CONTROL  40
#define UI_PAD      16

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
    uint64_t id;
    float t;
} Ui_Anim;

typedef struct {
    Font font, bold;
    bool dark;
    uint64_t active_id;   // widget that captured the mouse press
    uint64_t focus_id;    // text input that has the keyboard
    int cursor;           // mouse cursor to show at the end of the frame
    Ui_Anim anim[128];
} Ui;

static Ui ui;

const Theme *theme(void)
{
    return ui.dark ? &THEME_DARK : &THEME_LIGHT;
}

uint64_t ui_id(const char *file, int line, int extra)
{
    uint64_t h = 5381;
    for (const char *p = file; *p; ++p) h = h*33 + (uint8_t)*p;
    h = h*33 + (uint64_t)line;
    h = h*33 + (uint64_t)extra;
    return h ? h : 1;
}

#define UI_ID(extra) ui_id(__FILE__, __LINE__, (extra))

// Fonts are rasterised large and drawn smaller with mipmaps, which is what
// keeps them crisp on both regular and HiDPI screens.
Font ui_load_font(const unsigned char *data, size_t size)
{
    Font font = LoadFontFromMemory(".ttf", data, (int)size, 64, NULL, 0);
    GenTextureMipmaps(&font.texture);
    SetTextureFilter(font.texture, TEXTURE_FILTER_TRILINEAR);
    return font;
}

void ui_init(Font font, Font bold)
{
    ui.font = font;
    ui.bold = bold;
}

void ui_begin(void)
{
    ui.cursor = MOUSE_CURSOR_DEFAULT;
}

void ui_end(void)
{
    SetMouseCursor(ui.cursor);
    // A widget that captured the press but is no longer drawn (screen
    // changed, dialog closed) would otherwise keep the capture forever.
    // This runs after every widget had its chance to see the release.
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) ui.active_id = 0;
}

// Smoothly moves towards 1 while `on`, towards 0 otherwise.
static float ui_anim(uint64_t id, bool on)
{
    Ui_Anim *slot = &ui.anim[id % ARRAY_LEN(ui.anim)];
    if (slot->id != id) {
        slot->id = id;
        slot->t = 0;
    }
    float target = on ? 1.0f : 0.0f;
    float k = 16.0f*GetFrameTime();
    slot->t += (target - slot->t)*(k > 1.0f ? 1.0f : k);
    return slot->t;
}

static bool ui_hovered(Rectangle r)
{
    return CheckCollisionPointRec(GetMousePosition(), r);
}

// Press inside captures the widget, release inside counts as a click.
static bool ui_click(uint64_t id, Rectangle r)
{
    bool over = ui_hovered(r);
    if (over) ui.cursor = MOUSE_CURSOR_POINTING_HAND;
    if (ui.active_id == 0 && over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) ui.active_id = id;
    if (ui.active_id == id && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        ui.active_id = 0;
        return over;
    }
    return false;
}

static Color ui_lerp(Color a, Color b, float t)
{
    return (Color){
        a.r + (b.r - a.r)*t, a.g + (b.g - a.g)*t, a.b + (b.b - a.b)*t, a.a + (b.a - a.a)*t,
    };
}

static float ui_roundness(Rectangle r, float radius)
{
    float side = r.width < r.height ? r.width : r.height;
    if (side <= 0) return 0;
    float v = radius*2/side;
    return v > 1 ? 1 : v;
}

Vector2 ui_measure(const char *text, float size, bool bold)
{
    return MeasureTextEx(bold ? ui.bold : ui.font, text, size, 0);
}

void ui_text(const char *text, float x, float y, float size, Color color, bool bold)
{
    DrawTextEx(bold ? ui.bold : ui.font, text, (Vector2){x, y}, size, 0, color);
}

void ui_text_centered(const char *text, Rectangle r, float size, Color color, bool bold)
{
    Vector2 m = ui_measure(text, size, bold);
    ui_text(text, r.x + (r.width - m.x)/2, r.y + (r.height - m.y)/2, size, color, bold);
}

// A flat surface with a hairline border.
void ui_panel(Rectangle r, float radius, Color fill, Color border)
{
    DrawRectangleRounded(r, ui_roundness(r, radius), 10, fill);
    DrawRectangleRoundedLinesEx(r, ui_roundness(r, radius), 10, 1, border);
}

void ui_hline(float x, float y, float width)
{
    DrawRectangle((int)x, (int)y, (int)width, 1, theme()->border);
}

// A circle with the first letter of a name.
void ui_avatar(Vector2 center, float radius, const char *name, bool strong)
{
    const Theme *t = theme();
    DrawCircleV(center, radius, strong ? t->accent : t->accent_soft);
    char initial[2] = { name[0] ? (char)toupper((unsigned char)name[0]) : '?', '\0' };
    Rectangle r = { center.x - radius, center.y - radius, radius*2, radius*2 };
    ui_text_centered(initial, r, radius*1.1f, strong ? WHITE : t->accent, true);
}

bool ui_button_id(uint64_t id, Rectangle r, const char *label, bool primary)
{
    const Theme *t = theme();
    bool clicked = ui_click(id, r);
    float hover = ui_anim(id, ui_hovered(r) || ui.active_id == id);
    if (primary) {
        ui_panel(r, UI_RADIUS, ui_lerp(t->accent, t->accent_hi, hover), BLANK);
        ui_text_centered(label, r, UI_BODY, WHITE, true);
    } else {
        ui_panel(r, UI_RADIUS, ui_lerp(t->surface, t->surface2, hover), t->border);
        ui_text_centered(label, r, UI_BODY, t->text, true);
    }
    return clicked;
}

#define ui_button(r, label, primary) ui_button_id(UI_ID(0), (r), (label), (primary))

typedef struct {
    size_t cursor;    // byte offset into the buffer
    float scroll;     // pixels scrolled horizontally
    double blink;     // time of the last edit, keeps the caret visible
} Text_State;

static size_t utf8_prev(const char *s, size_t i)
{
    if (i == 0) return 0;
    --i;
    while (i > 0 && (s[i] & 0xC0) == 0x80) --i;
    return i;
}

static size_t utf8_next(const char *s, size_t i)
{
    if (s[i] == '\0') return i;
    ++i;
    while ((s[i] & 0xC0) == 0x80) ++i;
    return i;
}

static void ui_insert(char *buf, size_t cap, size_t *cursor, int codepoint)
{
    int n;
    const char *enc = CodepointToUTF8(codepoint, &n);
    size_t len = strlen(buf);
    if (len + n >= cap) return;
    memmove(buf + *cursor + n, buf + *cursor, len - *cursor + 1);
    memcpy(buf + *cursor, enc, n);
    *cursor += n;
}

#define KEY_REPEAT(k) (IsKeyPressed(k) || IsKeyPressedRepeat(k))

// Applies this frame's keyboard input to the buffer. True when Enter was hit.
static bool ui_edit(char *buf, size_t cap, Text_State *st)
{
    size_t len = strlen(buf);
    if (st->cursor > len) st->cursor = len;
    bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
               IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);

    if (KEY_REPEAT(KEY_LEFT))  st->cursor = utf8_prev(buf, st->cursor);
    if (KEY_REPEAT(KEY_RIGHT)) st->cursor = utf8_next(buf, st->cursor);
    if (IsKeyPressed(KEY_HOME)) st->cursor = 0;
    if (IsKeyPressed(KEY_END))  st->cursor = len;
    if (KEY_REPEAT(KEY_BACKSPACE) && st->cursor > 0) {
        size_t p = utf8_prev(buf, st->cursor);
        memmove(buf + p, buf + st->cursor, len - st->cursor + 1);
        st->cursor = p;
    }
    if (KEY_REPEAT(KEY_DELETE) && st->cursor < len) {
        size_t n = utf8_next(buf, st->cursor);
        memmove(buf + st->cursor, buf + n, len - n + 1);
    }
    if (mod) {
        if (IsKeyPressed(KEY_V)) {
            const char *clip = GetClipboardText();
            while (clip && *clip) {
                int n;
                int cp = GetCodepointNext(clip, &n);
                if (cp >= 32) ui_insert(buf, cap, &st->cursor, cp);
                clip += n;
            }
        }
        while (GetCharPressed() > 0) {} // swallow, the shortcut was the point
    } else {
        int cp;
        while ((cp = GetCharPressed()) > 0) {
            if (cp >= 32) ui_insert(buf, cap, &st->cursor, cp);
        }
    }
    if (GetKeyPressed() != 0) st->blink = GetTime();
    return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
}

// Single line text box. Returns true when Enter is pressed while focused.
// With `secret` every character is drawn as a star.
bool ui_input_id(uint64_t id, Rectangle r, const char *placeholder, char *buf, size_t cap,
                 Text_State *st, bool secret)
{
    const Theme *t = theme();
    const float pad = 12, size = UI_BODY;
    bool over = ui_hovered(r);
    if (over) ui.cursor = MOUSE_CURSOR_IBEAM;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (over) {
            ui.focus_id = id;
            st->cursor = strlen(buf);
            st->blink = GetTime();
        } else if (ui.focus_id == id) {
            ui.focus_id = 0;
        }
    }
    bool focused = ui.focus_id == id;
    bool submitted = focused && ui_edit(buf, cap, st);

    float focus = ui_anim(id, focused);
    ui_panel(r, UI_RADIUS, t->surface, ui_lerp(t->border, t->accent, focus));
    if (focus > 0.01f) DrawRectangleRoundedLinesEx(r, ui_roundness(r, UI_RADIUS), 10, 1, Fade(t->accent, focus));

    // What gets drawn: the buffer, or one star per character.
    char *shown = buf;
    if (secret) {
        shown = temp_alloc(strlen(buf) + 1);
        size_t n = 0;
        for (size_t i = 0; buf[i]; i = utf8_next(buf, i)) shown[n++] = '*';
        shown[n] = '\0';
    }
    size_t shown_cursor = st->cursor;
    if (secret) {
        shown_cursor = 0;
        for (size_t i = 0; i < st->cursor && buf[i]; i = utf8_next(buf, i)) shown_cursor++;
    }

    // Keep the caret in view.
    char saved = shown[shown_cursor];
    shown[shown_cursor] = '\0';
    float caret_x = ui_measure(shown, size, false).x;
    shown[shown_cursor] = saved;
    float avail = r.width - pad*2;
    float total = ui_measure(shown, size, false).x;
    if (caret_x - st->scroll > avail) st->scroll = caret_x - avail;
    if (caret_x < st->scroll) st->scroll = caret_x;
    float max_scroll = total > avail ? total - avail : 0;
    if (st->scroll > max_scroll) st->scroll = max_scroll;
    if (st->scroll < 0) st->scroll = 0;

    float text_y = r.y + (r.height - size)/2 - 1;
    BeginScissorMode((int)(r.x + pad), (int)r.y, (int)avail, (int)r.height);
    if (buf[0] == '\0' && !focused) {
        ui_text(placeholder, r.x + pad, text_y, size, t->muted, false);
    } else {
        ui_text(shown, r.x + pad - st->scroll, text_y, size, t->text, false);
    }
    if (focused && fmod(GetTime() - st->blink, 1.0) < 0.55) {
        DrawRectangle((int)(r.x + pad + caret_x - st->scroll), (int)(text_y - 1), 1, (int)size + 3, t->text);
    }
    EndScissorMode();
    return submitted;
}

#define ui_input(r, placeholder, buf, st)  ui_input_id(UI_ID(0), (r), (placeholder), (buf), sizeof(buf), (st), false)
#define ui_secret(r, placeholder, buf, st) ui_input_id(UI_ID(0), (r), (placeholder), (buf), sizeof(buf), (st), true)

// Draws text wrapped at max_width, or only measures it when draw is false.
// Returns the number of lines. Breaks at spaces, and inside words that are
// wider than the whole line.
int ui_wrap(const char *text, float size, float max_width, Vector2 at, Color color, bool draw)
{
    Font font = ui.font;
    float line_h = size*1.4f;
    char *line = temp_alloc(strlen(text) + 8);
    size_t n = 0;
    int lines = 0;

#define FLUSH(len) do { \
        char saved = line[len]; line[len] = '\0'; \
        if (draw) DrawTextEx(font, line, (Vector2){at.x, at.y + lines*line_h}, size, 0, color); \
        line[len] = saved; lines++; \
    } while (0)

    for (const char *p = text;;) {
        int cp_size = 1;
        int cp = *p ? GetCodepointNext(p, &cp_size) : 0;
        if (cp == 0 || cp == '\n') {
            FLUSH(n);
            n = 0;
            if (cp == 0) break;
            p += cp_size;
            continue;
        }
        if (n == 0 && cp == ' ') { p += cp_size; continue; }
        memcpy(line + n, p, cp_size);
        n += cp_size;
        line[n] = '\0';
        p += cp_size;

        while (MeasureTextEx(font, line, size, 0).x > max_width) {
            size_t cut = n;
            while (cut > 0 && line[cut - 1] != ' ') --cut;   // just after the last space
            size_t keep_from;
            if (cut > 0 && cut < n) {
                FLUSH(cut - 1);
                keep_from = cut;
            } else {
                keep_from = utf8_prev(line, n);
                if (keep_from == 0) break;                  // one glyph wider than the line
                FLUSH(keep_from);
            }
            n -= keep_from;
            memmove(line, line + keep_from, n + 1);
        }
    }
#undef FLUSH
    return lines;
}

float ui_line_height(float size)
{
    return size*1.4f;
}

#endif // UI_C_
