#pragma once
#define UU_FOCUS_MAIN


struct UUFocusMainCoroutine;

void uu_focus_main(UUFocusMainCoroutine* program);


#include <cstdint>

enum Command
{
    Command_null,
    Command_application_stop,
    Command_timer_start,
    Command_timer_stop,
};

struct CommandMsg
{
    Command type;
};

struct UUFocusMainCoroutine
{
    // inputs:
    struct
    {
        CommandMsg command;
        std::uint64_t time_micros;
    } input;

    // effects:
    struct AudioEffect *audio_effect;
    struct TimerEffect *timer_effect;

    // internal state:
    int step;
    std::uint64_t step_micros;
};

