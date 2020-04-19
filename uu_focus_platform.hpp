#pragma once

struct Platform;

// A piece of text content destined to the user
struct UIText
{
    char buf[16];
};

struct Civil_Time_Of_Day
{
  int hh;
  int mm;
};

// Trigger a re-render asynchronously
void platform_render_async(Platform*);

// Notify user of an important status
void platform_notify(Platform*, UIText content);

// What is the current day's time of day?
Civil_Time_Of_Day platform_get_time_of_day();

UIText ui_text_temp(char const* fmt, ...);

void temp_allocator_reset();
