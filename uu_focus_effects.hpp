#pragma once
#define UU_FOCUS_EFFECTS

struct Platform;

struct AudioEffect;
void audio_start(AudioEffect*);
void audio_stop(AudioEffect*);

// meant to be called by platform layer
void audio_thread_render(AudioEffect*, float* stereo_frames, int frame_count);

struct TimerEffect;
TimerEffect* timer_make(Platform* platform);
void timer_destroy(TimerEffect*);

void timer_start(TimerEffect*);
void timer_stop(TimerEffect*);
void timer_reset(TimerEffect*);
bool timer_is_active(TimerEffect*);
void timer_update_and_render(TimerEffect*);
void timer_celebrate(TimerEffect*);
