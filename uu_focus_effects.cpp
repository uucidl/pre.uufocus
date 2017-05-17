#include "uu_focus_effects.hpp"

void audio_start(AudioEffect*) {}
void audio_stop(AudioEffect*) {}

void timer_start(TimerEffect*) {}
void timer_stop(TimerEffect*) {}
bool timer_is_active(TimerEffect*) { return true; }
void timer_update_and_render(TimerEffect*) {}
void timer_reset(TimerEffect*) {}
void timer_celebrate(TimerEffect*) {}
