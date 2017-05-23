#include "uu_focus_effects.hpp"
#include "uu_focus_platform.hpp"

void audio_start(AudioEffect*) {}
void audio_stop(AudioEffect*) {}

struct TimerEffect
{
    int on_count;
    Platform* platform;
};

TimerEffect* timer_make(Platform* platform)
{
    auto _timer = new TimerEffect;
    auto &timer = *_timer;
    timer = {};
    timer.platform = platform;
    return &timer;
}

void timer_start(TimerEffect* _timer)
{
    auto& timer = *_timer;
    ++timer.on_count;
    timer_update_and_render(&timer);
}

void timer_stop(TimerEffect* _timer)
{
    auto& timer = *_timer;
    --timer.on_count;
    timer_update_and_render(&timer);
}

bool timer_is_active(TimerEffect* _timer)
{
    auto& timer = *_timer;
    return timer.on_count > 0;
}

void timer_update_and_render(TimerEffect* _timer)
{
    auto& timer = *_timer;
    platform_render(timer.platform);
}

void timer_reset(TimerEffect* _timer)
{
    auto& timer = *_timer;
    timer_update_and_render(&timer);
}

void timer_celebrate(TimerEffect* _timer)
{
    auto& timer = *_timer;
    timer_update_and_render(&timer);
}
