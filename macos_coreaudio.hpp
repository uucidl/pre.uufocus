#pragma once

#include <stdint.h>

#define COREAUDIO_STREAM_RENDER(name_expr) \
  CoreaudioStreamError name_expr(          \
    uint64_t presentation_time_micros,     \
    float* frames,                         \
    int frames_n)

enum CoreaudioStreamError
{
  CoreaudioStreamError_Success,
  CoreaudioStreamError_PastTheEnd,
  CoreaudioStreamError_SystemError,
};

typedef COREAUDIO_STREAM_RENDER(CoreaudioStreamInputRenderFn);

struct CoreaudioStreamHeader
{
  uint64_t mach_absolute_time_origin; // pass in the origin to use for the clock
  CoreaudioStreamInputRenderFn* input_render; // pass your render function
  CoreaudioStreamError error; // current error code
  char const* error_string;
};

struct CoreaudioStream
{
  CoreaudioStreamHeader header;
  char data[32];
};

CoreaudioStreamError
macos_coreaudio_open_stereo(CoreaudioStream*, int audio_hz);

void
macos_coreaudio_close(CoreaudioStream*);

