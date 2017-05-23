#pragma once
#define UU_FOCUS_EFFECTS

struct Platform;

struct AudioEffect;
void audio_start(AudioEffect*);
void audio_stop(AudioEffect*);

struct TimerEffect;
TimerEffect* timer_make(Platform* platform);
void timer_destroy(TimerEffect*);

void timer_start(TimerEffect*);
void timer_stop(TimerEffect*);
void timer_reset(TimerEffect*);
bool timer_is_active(TimerEffect*);
void timer_update_and_render(TimerEffect*);
void timer_celebrate(TimerEffect*);

