/*
  playing a stereo stream on windows using WASAPI

  Introduction
  http://msdn.microsoft.com/en-us/library/windows/desktop/dd371455(v=vs.85).aspx

  Recovering on errors
  http://msdn.microsoft.com/en-us/library/windows/desktop/dd316605(v=vs.85).aspx

  TODO(nicolas): remove more macros usage, clarify functions
  TODO(nicolas): what are actionable errors and those who aren't?
  TODO(nicolas): what are errors worth logging?
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
    WasapiStreamHeader header;
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

// TODO(nicolas): remove unnecessary macros

#define fatal_msg(error_string_expr) (cpu_debugbreak(), state_header.error_string = error_string_expr)

#define OS_SUCCESS(call) (!FAILED(call))
#define THEN_DO(expr) ((expr), 1)
#define FAIL_WITH(error_string_expr) (THEN_DO(fatal_msg(error_string_expr)) && 0)
#define RETURN_ON_ERROR(expr)                                                   \
    if (!(expr)) {                                                             \
        return;                                                                \
    }
#define RETURN_ON_ERROR_WITH(expr, value)                                       \
    if (!(expr)) {                                                             \
        return value;                                                          \
    }

WasapiStreamError
win32_wasapi_sound_open_stereo(WasapiStream *_state, int const audio_hz)
{
    WasapiStreamValue state = {};
    memcpy(_state, &state, sizeof state);

    auto& state_header = _state->header;
    state_header.error = WasapiStreamError_SystemError;

    HRESULT hr;
    hr = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
    if (hr < 0) { FAIL_WITH("could not initialize COM"); return state_header.error; }

    IAudioClient *audio_client = nullptr;
    {
        IMMDeviceEnumerator *device_enumerator = nullptr;
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator),
                              (void **)&device_enumerator);
        if (hr < 0) { FAIL_WITH("could not get enumerator"); }
        IMMDevice *output_device = nullptr;
        if (device_enumerator) {
            hr = device_enumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                            &output_device);
            if (hr < 0) { FAIL_WITH("could not get default device"); }
            device_enumerator->Release();
        }
        if (output_device) {
            hr = output_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL,
                                         (void **)&audio_client);
            if (hr < 0) { FAIL_WITH("could not activate audio client"); }
            output_device->Release();
        }
    }
    if (!audio_client) return state_header.error;

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
    if (hr < 0) { FAIL_WITH("could not get supported format"); return state_header.error; }

    if (S_FALSE == hr) {
        if (format->cbSize != closest_format->cbSize) {
            FAIL_WITH("unexpected format type");
            return state_header.error;
        }
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
    if (hr < 0) { FAIL_WITH("could not initialize audio client"); return state_header.error; }

    if (AUDCLNT_STREAMFLAGS_RATEADJUST & stream_flags) {
        IAudioClockAdjustment *clock_adjustment;

        hr = audio_client->GetService(__uuidof(IAudioClockAdjustment),
                                      (void **)&clock_adjustment);
        if (hr < 0) { FAIL_WITH("could not get clock adjustment service"); return state_header.error; }
        hr = clock_adjustment->SetSampleRate((float)audio_hz);
        if (hr < 0) { FAIL_WITH("could not adjust sample rate"); return state_header.error; }
    }

    // TODO(nicolas): don't leak resources on error cases
    UINT32 frame_count;
    hr = audio_client->GetBufferSize(&frame_count);
    if (hr < 0) { FAIL_WITH("could not get total frame count"); return state_header.error; }
    state.max_frame_count = frame_count;
    state.refill_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    state.audio_client = audio_client;

    hr = audio_client->SetEventHandle(state.refill_event);
    if (hr < 0) { FAIL_WITH("could not setup callback event"); return state_header.error; }

    audio_client->Start();

    IAudioRenderClient *render_client;
    hr = state.audio_client->GetService(__uuidof(IAudioRenderClient),
                                         (void **)&render_client);
    if (hr < 0) { FAIL_WITH("could not get render client"); return state_header.error; }
    state.render_client = render_client;

    memcpy(_state, &state, sizeof state);
    return _state->header.error;
}

void
win32_wasapi_sound_close(WasapiStream* _state)
{
    WasapiStreamValue state;
    memcpy(&state, _state, sizeof state);

    if (state.header.error != WasapiStreamError_Success) return;

    state.audio_client->Stop();
    state.audio_client->Release();
    state.audio_client = nullptr;

    state.render_client = nullptr;
    CloseHandle(state.refill_event);
    state.refill_event = 0;
    state.header.error = WasapiStreamError_Closed;
    memcpy(_state, &state, sizeof state);
}


static WasapiBuffer
wasapi_buffer_acquire(WasapiStreamHeader* _state_header, WasapiStreamValue const& state, uint32_t max_buffer_frame_count)
{
    WasapiStreamHeader& state_header = *_state_header;
    WasapiBuffer result = {};
    HRESULT hr;
    UINT32 frame_end;
    hr = state.audio_client->GetBufferSize(&frame_end);
    RETURN_ON_ERROR_WITH(OS_SUCCESS(hr) ||
                               FAIL_WITH("could not get total frame count"),
                        result);

    UINT32 frame_start;
    hr = state.audio_client->GetCurrentPadding(&frame_start);
    RETURN_ON_ERROR_WITH(
        OS_SUCCESS(hr) || FAIL_WITH("could not get frame start"), result);

    UINT32 frame_count = frame_end - frame_start;
    if (frame_count > max_buffer_frame_count) {
        frame_count = max_buffer_frame_count;
    }
    if (frame_count > 0) {
        BYTE *buffer;
        hr = state.render_client->GetBuffer(frame_count, &buffer);
        if (hr == S_OK) {
            result.bytes_first = buffer;
            result.frame_count = frame_count;
        } else if (hr != AUDCLNT_E_BUFFER_ERROR) {
            RETURN_ON_ERROR_WITH(
                OS_SUCCESS(hr) ||
                FAIL_WITH("could not get buffer"),
                result);
        }
    }
    return result;
}

WasapiBuffer
win32_wasapi_sound_buffer_acquire(WasapiStream* _state, uint32_t max_frame_count)
{
    WasapiStreamValue state_value;
    memcpy(&state_value, _state, sizeof state_value);
    return wasapi_buffer_acquire(&_state->header, state_value, max_frame_count);
}

WasapiBuffer
win32_wasapi_sound_buffer_block_acquire(WasapiStream *_state, uint32_t max_frame_count)
{
    WasapiStreamValue state_value;
    memcpy(&state_value, _state, sizeof state_value);
    WaitForSingleObject(state_value.refill_event, INFINITE);
    return wasapi_buffer_acquire(&_state->header, state_value, max_frame_count);
}

void
win32_wasapi_sound_buffer_release(WasapiStream *_state, WasapiBuffer buffer)
{
    WasapiStreamValue state_value;
    memcpy(&state_value, _state, sizeof state_value);
    WasapiStreamHeader& state_header = _state->header;
    auto const& state = state_value;
    if (buffer.frame_count > 0) {
        HRESULT hr = state.render_client->ReleaseBuffer(buffer.frame_count, 0);
        RETURN_ON_ERROR(OS_SUCCESS(hr) ||
                       FAIL_WITH("could not release buffer"));
    }
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#undef fatal_msg
#undef OS_SUCCESS
#undef THEN_DO
#undef FAIL_WITH
#undef RETURN_ON_ERROR
#undef RETURN_ON_ERROR_WITH
