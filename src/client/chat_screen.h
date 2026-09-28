#ifndef SOCKET_CHAT_CHAT_SCREEN_H
#define SOCKET_CHAT_CHAT_SCREEN_H

#include "ui/ui_widgets.h"

#define CHAT_SCREEN_MESSAGE_CAPACITY 512u

typedef struct ChatScreen
{
    char draft[CHAT_SCREEN_MESSAGE_CAPACITY];
    UiTextInputState draft_input;
    UiButtonState send_button;
    int selected_conversation;
} ChatScreen;

void chat_screen_init(ChatScreen *screen);
void chat_screen_draw(ChatScreen *screen);

#endif
