#pragma once

#include <stdint.h>

struct WasapiStream
{
    char data[64];
};

struct WasapiBuffer
{
    int is_valid;
    uint8_t* bytes_first;
    uint32_t frame_count;
};

void
win32_wasapi_sound_open_stereo(WasapiStream*, int audio_hz);

void
win32_wasapi_sound_close(WasapiStream*);

WasapiBuffer
win32_wasapi_sound_buffer_block_acquire(WasapiStream*, uint32_t max_frame_count);

WasapiBuffer
win32_wasapi_sound_buffer_acquire(WasapiStream*, uint32_t max_frame_count);

void
win32_wasapi_sound_buffer_release(WasapiStream*, WasapiBuffer);

