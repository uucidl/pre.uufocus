#pragma once

#include <stdint.h>

struct TimerEffect
{
    int on_count;
    uint64_t now_micros;
    uint64_t end_micros;
    Platform* platform;
};



