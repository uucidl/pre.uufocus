#pragma once

#include <stdint.h>

enum WasapiStreamError
{
    WasapiStreamError_Success,
    WasapiStreamError_Closed,
    WasapiStreamError_SystemError,
};

// TODO(nil): the name Header is descriptive of something. Maybe
// a misrepresentation.

struct WasapiStreamHeader
{
    WasapiStreamError error;
    char* error_string;
};

struct WasapiStream
{
    WasapiStreamHeader header;
    char data[64];
};

struct WasapiBuffer
{
    uint8_t* bytes_first;
    uint32_t frame_count;
};

WasapiStreamError
win32_wasapi_sound_open_stereo(WasapiStream*, int audio_hz);

void
win32_wasapi_sound_close(WasapiStream*);

WasapiBuffer
win32_wasapi_sound_buffer_acquire(WasapiStream*, uint32_t max_frame_count);

void
win32_wasapi_sound_buffer_release(WasapiStream*, WasapiBuffer);

// wait until the stream needs a buffer refill then acquire a render buffer
WasapiBuffer
win32_wasapi_sound_buffer_block_acquire(WasapiStream*, uint32_t max_frame_count);

