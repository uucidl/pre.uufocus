/*
  playing a stereo stream on windows using WASAPI

  Introduction
  http://msdn.microsoft.com/en-us/library/windows/desktop/dd371455(v=vs.85).aspx

  Recovering on errors
  http://msdn.microsoft.com/en-us/library/windows/desktop/dd316605(v=vs.85).aspx
*/

#include "win32_wasapi_sound.hpp"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
#endif

#include <Audioclient.h>
#include <Mmdeviceapi.h>
#pragma comment(lib, "ole32.lib")

struct WasapiStreamValue
{
    bool is_valid;
    HANDLE refill_event;
    IAudioClient *audio_client;
    IAudioRenderClient *render_client;
    uint32_t max_frame_count;
};

#if !defined(cpu_debugbreak)
#if defined(_MSC_VER)
#define cpu_debugbreak() __debugbreak()
#else
#error define cpu_debugbreak
#endif
#endif

#if !defined(os_abort)
void abort();
#define os_abort() abort()
#endif

#define fatal_msg(...) (cpu_debugbreak(), os_abort(), 1)

#define OS_SUCCESS(call) (!FAILED(call))
#define THEN_DO(expr) ((expr), 1)
#define FAIL_WITH(...) (THEN_DO(fatal_msg(__VA_ARGS__)) && 0)
#define BREAK_ON_ERROR(expr)                                                   \
    if (!(expr)) {                                                             \
        return;                                                                \
    }
#define BREAK_ON_ERROR_WITH(expr, value)                                       \
    if (!(expr)) {                                                             \
        return value;                                                          \
    }
#define EFFECT(expr) (void) expr

void
win32_wasapi_sound_open_stereo(WasapiStream *_state, int const audio_hz)
{
    WasapiStreamValue state = {};

    HRESULT hr;
    hr = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
    BREAK_ON_ERROR(OS_SUCCESS(hr) || FAIL_WITH("could not initialize COM\n"));

    IMMDeviceEnumerator *device_enumerator;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator),
                          (void **)&device_enumerator);
    BREAK_ON_ERROR(OS_SUCCESS(hr) || FAIL_WITH("could not get enumerator\n"));

    // TODO(nicolas): aren't we leaking the enumerator here? Yes if and only if
    // the FAIL_WITH macro isn't fatal.

    IMMDevice *default_device;
    hr = device_enumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                    &default_device);
    BREAK_ON_ERROR(OS_SUCCESS(hr) ||
                          FAIL_WITH("could not get default device\n"));

    IAudioClient *audio_client;
    hr = default_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL,
                                  (void **)&audio_client);
    BREAK_ON_ERROR(OS_SUCCESS(hr) || FAIL_WITH("could not get audio client\n"));

    WAVEFORMATEXTENSIBLE formatex = {};
    {
        formatex.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        formatex.Format.nChannels = 2;
        formatex.Format.nSamplesPerSec = audio_hz;
        formatex.Format.nAvgBytesPerSec = audio_hz * sizeof(float) * 2;
        formatex.Format.nBlockAlign = sizeof(float) * 2;
        formatex.Format.wBitsPerSample = sizeof(float) * 8;
        formatex.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        formatex.Samples.wValidBitsPerSample = sizeof(float) * 8;
        formatex.dwChannelMask = SPEAKER_ALL;
        formatex.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }
    WAVEFORMATEX *format = &formatex.Format;
    WAVEFORMATEX *closest_format;
    hr = audio_client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, format,
                                         &closest_format);
    BREAK_ON_ERROR(OS_SUCCESS(hr) ||
                          FAIL_WITH("could not get supported format\n"));

    BREAK_ON_ERROR((AUDCLNT_E_UNSUPPORTED_FORMAT != hr) ||
                          FAIL_WITH("format definitely not supported\n"));

    if (S_FALSE == hr) {
        BREAK_ON_ERROR(format->cbSize == closest_format->cbSize ||
                              FAIL_WITH("unexpected format type\n"));
        formatex = *((WAVEFORMATEXTENSIBLE *)closest_format);
        CoTaskMemFree(closest_format);
    }

    DWORD demanded_audio_hz = DWORD(audio_hz);
    DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    if (format->nSamplesPerSec != demanded_audio_hz) {
        stream_flags |= AUDCLNT_STREAMFLAGS_RATEADJUST;
    }

    hr = audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, stream_flags, 0, 0,
                                  format, NULL);
    BREAK_ON_ERROR(OS_SUCCESS(hr) ||
                          FAIL_WITH("could not initialize audio client\n"));

    if (AUDCLNT_STREAMFLAGS_RATEADJUST & stream_flags) {
        IAudioClockAdjustment *clock_adjustment;

        hr = audio_client->GetService(__uuidof(IAudioClockAdjustment),
                                      (void **)&clock_adjustment);
        BREAK_ON_ERROR(OS_SUCCESS(hr) ||
                              FAIL_WITH("could not get clock adjustment service\n"));
        hr = clock_adjustment->SetSampleRate((float)audio_hz);
        BREAK_ON_ERROR(
            OS_SUCCESS(hr) ||
            FAIL_WITH("could not adjust sample rate to %d\n", audio_hz));
    }

    UINT32 frame_count;
    hr = audio_client->GetBufferSize(&frame_count);
    BREAK_ON_ERROR(OS_SUCCESS(hr) ||
                          FAIL_WITH("could not get total frame count\n"));
    state.max_frame_count = frame_count;
    state.refill_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    state.audio_client = audio_client;

    hr = audio_client->SetEventHandle(state.refill_event);
    BREAK_ON_ERROR(OS_SUCCESS(hr) ||
                          FAIL_WITH("could not setup callback event\n"));

    audio_client->Start();

    IAudioRenderClient *render_client;
    hr = state.audio_client->GetService(__uuidof(IAudioRenderClient),
                                         (void **)&render_client);
    BREAK_ON_ERROR(OS_SUCCESS(hr) ||
                          FAIL_WITH("could not get render client\n"));
    state.render_client = render_client;
    state.is_valid = true;

    memcpy(_state, &state, sizeof state);
}

void
win32_wasapi_sound_close(WasapiStream* _state)
{
    WasapiStreamValue state;
    memcpy(&state, _state, sizeof state);

    if (state.is_valid)
    {
        state.audio_client->Stop();
        state.audio_client->Release();
        state.audio_client = nullptr;

        state.render_client = nullptr;
        CloseHandle(state.refill_event);
        state.refill_event = 0;
        state.is_valid = false;
    }
    memcpy(_state, &state, sizeof state);
}


static WasapiBuffer
wasapi_buffer_acquire(WasapiStreamValue const& state, uint32_t max_buffer_frame_count)
{
    WasapiBuffer result = {};
    HRESULT hr;
    UINT32 frame_end;
    hr = state.audio_client->GetBufferSize(&frame_end);
    BREAK_ON_ERROR_WITH(OS_SUCCESS(hr) ||
                               FAIL_WITH("could not get total frame count\n"),
                        result);

    UINT32 frame_start;
    hr = state.audio_client->GetCurrentPadding(&frame_start);
    BREAK_ON_ERROR_WITH(
        OS_SUCCESS(hr) || FAIL_WITH("could not get frame start\n"), result);

    UINT32 frame_count = frame_end - frame_start;
    if (frame_count > max_buffer_frame_count) {
        frame_count = max_buffer_frame_count;
    }
    BYTE *buffer;
    hr = state.render_client->GetBuffer(frame_count, &buffer);
    if (hr != AUDCLNT_E_BUFFER_ERROR) {
        BREAK_ON_ERROR_WITH(
            OS_SUCCESS(hr) ||
                FAIL_WITH("could not get buffer [%d]\n", iterations),
            result);
        result.bytes_first = buffer;
        result.frame_count = frame_count;
        result.is_valid = true;
    }
    return result;
}

WasapiBuffer
win32_wasapi_sound_buffer_acquire(WasapiStream* _state, uint32_t max_frame_count)
{
    WasapiStreamValue _state_value;
    memcpy(&_state_value, _state, sizeof _state_value);
    auto const& state = _state_value;
    return wasapi_buffer_acquire(state, max_frame_count);
}

WasapiBuffer
win32_wasapi_sound_buffer_block_acquire(WasapiStream *_state, uint32_t max_frame_count)
{
    WasapiStreamValue _state_value;
    memcpy(&_state_value, _state, sizeof _state_value);
    auto const& state = _state_value;
    WaitForSingleObject(state.refill_event, INFINITE);
    return wasapi_buffer_acquire(state, max_frame_count);
}


void
win32_wasapi_sound_buffer_release(WasapiStream *_state, WasapiBuffer buffer)
{
    WasapiStreamValue _state_value;
    memcpy(&_state_value, _state, sizeof _state_value);
    auto const& state = _state_value;
    if (buffer.is_valid) {
        HRESULT hr = state.render_client->ReleaseBuffer(buffer.frame_count, 0);
        BREAK_ON_ERROR(OS_SUCCESS(hr) ||
                       FAIL_WITH("could not release buffer\n"));
    }
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif