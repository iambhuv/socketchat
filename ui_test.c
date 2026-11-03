// Headless geometry checks: no window, network, or personal database.
#define main chat_app_main
#include "src/chat.c"
#undef main
static Clay_Dimensions measure(Clay_StringSlice s, Clay_TextElementConfig *cfg, void *p) {
    (void)p; return (Clay_Dimensions){s.length * cfg->fontSize * 0.5f, cfg->fontSize};
}
static Clay_BoundingBox text_box(Clay_RenderCommandArray commands, const char *label) {
    for (int i = 0; i < commands.length; ++i) {
        Clay_RenderCommand *r = Clay_RenderCommandArray_Get(&commands, i);
        Clay_StringSlice s = r->renderData.text.stringContents;
        if (r->commandType == CLAY_RENDER_COMMAND_TYPE_TEXT && s.length == (int)strlen(label) && !memcmp(s.chars, label, s.length)) return r->boundingBox;
    }
    return (Clay_BoundingBox){0};
}
int main(void) {
    uint32_t size = Clay_MinMemorySize();
    Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(size, malloc(size)), (Clay_Dimensions){960,600}, (Clay_ErrorHandler){.errorHandlerFunction=ui_clay_error});
    Clay_SetMeasureTextFunction(measure, NULL);
    Chat c = {.client = {.sock = NET_INVALID}};
    strcpy(c.nickname, "Layout test");
    for (int width = 720; width <= 960; width += 240) {
        Clay_SetLayoutDimensions((Clay_Dimensions){width,480});
        Clay_BeginLayout(); chats_screen(&c);
        Clay_RenderCommandArray cmds = Clay_EndLayout();
        Clay_BoundingBox pick = text_box(cmds, "Pick a conversation");
        if (pick.x < SIDEBAR_WIDTH || pick.width <= 0) { fprintf(stderr,"Conversation overlaps sidebar: x=%f width=%f\n",pick.x,pick.width); return 1; }
        c.chat_open = true;
        Clay_BeginLayout(); chats_screen(&c); cmds = Clay_EndLayout();
        Clay_BoundingBox send = text_box(cmds,"Send");
        Clay_BoundingBox draft = text_box(cmds,"Write a message");
        if (draft.width < 100 || draft.x < SIDEBAR_WIDTH || draft.x + draft.width > send.x) { fprintf(stderr,"Composer input collapsed\n"); return 1; }
        if (send.x < width - 110 || send.x < SIDEBAR_WIDTH || send.y + send.height > 480 || send.width <= 0) { fprintf(stderr,"Composer outside conversation: %f,%f\n",send.x,send.y); return 1; }
        c.chat_open = false;
    }
    puts("UI layout checks passed (720 and 960 pixels)");
}
