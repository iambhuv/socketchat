#ifndef SOCKET_CHAT_ONBOARDING_SCREEN_H
#define SOCKET_CHAT_ONBOARDING_SCREEN_H

#include "protocol/protocol.h"
#include "ui/ui_widgets.h"

#include <stdbool.h>

#define ONBOARDING_USERNAME_CAPACITY (PROTOCOL_MAX_USERNAME_SIZE + 1u)
#define ONBOARDING_STATUS_CAPACITY 64u

typedef struct OnboardingScreen
{
    char username[ONBOARDING_USERNAME_CAPACITY];
    char status_text[ONBOARDING_STATUS_CAPACITY];
    UiTextInputState username_input;
    UiButtonState continue_button;
    UiStatusKind status_kind;
    bool username_has_error;
    float entrance_amount;
} OnboardingScreen;

void onboarding_screen_init(OnboardingScreen *screen);

/* Returns true when a valid username is submitted. */
bool onboarding_screen_draw(OnboardingScreen *screen);

const char *onboarding_screen_username(const OnboardingScreen *screen);
void onboarding_screen_set_username(OnboardingScreen *screen, const char *username);
void onboarding_screen_set_status(
    OnboardingScreen *screen,
    UiStatusKind kind,
    const char *text);

#endif
