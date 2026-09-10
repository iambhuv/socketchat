#ifndef SOCKET_CHAT_ONBOARDING_SCREEN_H
#define SOCKET_CHAT_ONBOARDING_SCREEN_H

#include "protocol/protocol.h"
#include "ui/ui_widgets.h"

#include <stdbool.h>

#define ONBOARDING_USERNAME_CAPACITY (PROTOCOL_MAX_USERNAME_SIZE + 1u)

typedef struct OnboardingScreen
{
    char username[ONBOARDING_USERNAME_CAPACITY];
    UiTextInputState username_input;
    UiButtonState continue_button;
    bool username_has_error;
    float entrance_amount;
} OnboardingScreen;

void onboarding_screen_init(OnboardingScreen *screen);

/* Returns true when a valid username is submitted. */
bool onboarding_screen_draw(OnboardingScreen *screen);

const char *onboarding_screen_username(const OnboardingScreen *screen);

#endif
