#ifndef SOCKET_CHAT_UI_ANIMATION_H
#define SOCKET_CHAT_UI_ANIMATION_H

float ui_clamp(float value, float minimum, float maximum);
float ui_lerp(float start, float end, float amount);

/* Moves current toward target at a consistent speed on different frame rates. */
float ui_smooth(float current, float target, float speed, float delta_time);

#endif
