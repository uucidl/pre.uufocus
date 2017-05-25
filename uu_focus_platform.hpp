#pragma once

struct Platform;

// A piece of text content destined to the user
struct UIText
{
    char data[16];
};

// Trigger a re-render asynchronously
void platform_render_async(Platform*);

// Notify user of an important status
void platform_notify(Platform*, UIText content);

UIText ui_text(char const* str);
