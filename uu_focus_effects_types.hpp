#pragma once

#include <stdint.h>

struct TimerEffect
{
    int on_count;
    uint64_t now_micros;
    uint64_t end_micros;
    Platform* platform;
    uint64_t start_micros;
    struct {
        // as civil time.
        int hours;
        int minutes;
    } start_time;
};



