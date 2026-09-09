#include "ui/ui_animation.h"

#include <math.h>

float ui_clamp(float value, float minimum, float maximum)
{
    if (value < minimum)
    {
        return minimum;
    }

    if (value > maximum)
    {
        return maximum;
    }

    return value;
}

float ui_lerp(float start, float end, float amount)
{
    return start + (end - start) * ui_clamp(amount, 0.0f, 1.0f);
}

float ui_smooth(float current, float target, float speed, float delta_time)
{
    float amount;

    if (delta_time <= 0.0f)
    {
        return current;
    }

    if (speed <= 0.0f)
    {
        return target;
    }

    amount = 1.0f - expf(-speed * delta_time);
    return ui_lerp(current, target, amount);
}
