/*
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

#include <cmath>

struct WasapiEndpointNotificationClient;

struct WasapiDevices
{
    bool is_initialized;
    IMMDeviceEnumerator *device_enumerator;
    WasapiEndpointNotificationClient* endpoint_notification_client;
};

struct WasapiStreamValue
{
    WasapiStreamHeader header;
    HANDLE refill_event;
    IAudioRenderClient *render_client;
    IAudioClient *audio_client;
    uint32_t output_device_version;
    uint32_t max_frame_count;
    int audio_hz;
};
static_assert(sizeof (WasapiStreamValue) < sizeof (WasapiStream),
              "WasapiStream is too small");

#if !defined(WIN32_WASAPI_SOUND_INTERNAL)
#define WIN32_WASAPI_SOUND_INTERNAL 1
#endif

#if WIN32_WASAPI_SOUND_INTERNAL
#if !defined(cpu_debugbreak)
#define win32_wasapi_own_cpu_debugbreak
#if defined(_MSC_VER)
#define cpu_debugbreak() __debugbreak()
#else
#error define cpu_debugbreak
#endif
#endif
#else
#define cpu_debugbreak() (void)1
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

static WasapiEndpointNotificationClient*
endpoint_notifications_acquire(IMMDeviceEnumerator* device_enumerator);

static void
endpoint_notifications_release(WasapiEndpointNotificationClient* client);

static uint32_t
notification_client_get_default_device_version(WasapiEndpointNotificationClient* client);

static WasapiDevices win32_wasapi_devices;

static int win32_wasapi_devices_acquire(WasapiDevices* devices, char const** error)
{
    *error = nullptr;

    HRESULT hr;
    hr = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
    if (hr < 0) { *error = "could not initialize COM"; return hr; }

    IMMDeviceEnumerator *device_enumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator),
                          (void **)&device_enumerator);
    if (hr < 0) { *error = "could not get enumerator"; return hr; }

    devices->device_enumerator = device_enumerator;
    devices->endpoint_notification_client = endpoint_notifications_acquire(device_enumerator);
    devices->is_initialized = true;
    return 0;
}

static void win32_wasapi_devices_release(WasapiDevices* devices)
{
    endpoint_notifications_release(devices->endpoint_notification_client);
    devices->device_enumerator->Release();
    *devices = {};
}

WasapiStreamError
win32_wasapi_sound_open_stereo(WasapiStream *_state, int const audio_hz)
{
    WasapiStreamValue state = {};
    memcpy(_state, &state, sizeof state);

    auto& state_header = _state->header;
    state_header.error = WasapiStreamError_SystemError;

    const auto fail = [_state](char const* error_string) {
        auto& state_header = _state->header;
        state_header.error_string = error_string;
        return state_header.error;
    };

    HRESULT hr;
    WAVEFORMATEXTENSIBLE formatex = {};

    if (!win32_wasapi_devices.is_initialized)
    {
        char const* error_string;
        win32_wasapi_devices_acquire(&win32_wasapi_devices, &error_string);
        if (error_string) {
            return fail(error_string);
        }
    }

    uint32_t const output_device_version =
        notification_client_get_default_device_version(
        win32_wasapi_devices.endpoint_notification_client);

    IAudioClient *audio_client = nullptr;
    /* obtain audio_client */ {
        auto device_enumerator = win32_wasapi_devices.device_enumerator;
        IMMDevice *output_device = nullptr;
        hr = device_enumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                        &output_device);
        if (!output_device || hr < 0) {
            cpu_debugbreak();
            fail("could not get default device");
        } else {
            hr = output_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL,
                                         (void **)&audio_client);
            if (!audio_client || hr < 0) {
                cpu_debugbreak();
                fail("could not activate audio client");
            }
            output_device->Release();
        }
    }
    if (!audio_client) {
        goto end_in_error;
    }

    /* initialize formatex */
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
    bool format_is_supported = false;
    WAVEFORMATEX *format = &formatex.Format;
    WAVEFORMATEX *closest_format = nullptr;
    hr = audio_client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, format,
                                         &closest_format);
    if (S_FALSE == hr) {
        if (format->cbSize != closest_format->cbSize) {
            cpu_debugbreak();
            fail("unexpected format type");
        } else {
            formatex = *((WAVEFORMATEXTENSIBLE *)closest_format);
            format_is_supported = true;
        }
    } else if (hr == S_OK) {
        format_is_supported = true;
    } else {
        cpu_debugbreak();
        fail("could not get supported format");
    }

    if (closest_format) {
        CoTaskMemFree(closest_format);
    }

    if (!format_is_supported) {
        goto end_in_error_with_audio_client;
    }

    DWORD demanded_audio_hz = DWORD(audio_hz);
    DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    if (format->nSamplesPerSec != demanded_audio_hz) {
        stream_flags |= AUDCLNT_STREAMFLAGS_RATEADJUST;
    }

    hr = audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, stream_flags, 0, 0,
                                  format, NULL);
    if (hr < 0) {
        cpu_debugbreak();
        fail("could not initialize audio client");
        goto end_in_error_with_audio_client;
    }

    bool stream_rate_adjusted = !(AUDCLNT_STREAMFLAGS_RATEADJUST & stream_flags);
    if (!stream_rate_adjusted) {
        IAudioClockAdjustment *clock_adjustment;

        hr = audio_client->GetService(__uuidof(IAudioClockAdjustment),
                                      (void **)&clock_adjustment);
        if (hr < 0) {
            cpu_debugbreak();
            fail("could not get clock adjustment service");
        } else {
            hr = clock_adjustment->SetSampleRate((float)audio_hz);
            if (hr < 0) {
                cpu_debugbreak();
                fail("could not adjust sample rate");
            } else {
                clock_adjustment->Release();
                stream_rate_adjusted = true;
            }
        }
    }
    if (!stream_rate_adjusted) {
        goto end_in_error_with_audio_client;
    }

    UINT32 frame_count;
    hr = audio_client->GetBufferSize(&frame_count);
    if (hr < 0) {
        cpu_debugbreak();
        fail("could not get total frame count");
        goto end_in_error_with_audio_client_started;
    }
    state.max_frame_count = frame_count;
    state.audio_hz = demanded_audio_hz;
    state.refill_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    state.audio_client = audio_client;
    state.output_device_version = output_device_version;

    hr = audio_client->SetEventHandle(state.refill_event);
    if (hr < 0) {
        cpu_debugbreak();
        fail("could not setup callback event");
        goto end_in_error_with_audio_client;
    }

    audio_client->Start();

    IAudioRenderClient *render_client;
    hr = state.audio_client->GetService(__uuidof(IAudioRenderClient),
                                        (void **)&render_client);
    if (hr < 0) {
        cpu_debugbreak();
        fail("could not get render client");
        goto end_in_error_with_audio_client_started;
    }
    state.render_client = render_client;

    memcpy(_state, &state, sizeof state);
    return _state->header.error;

    end_in_error_with_audio_client_started:
    audio_client->Stop();

    end_in_error_with_audio_client:
    audio_client->Release();

    end_in_error:
    return state_header.error;
}

void
win32_wasapi_sound_close(WasapiStream* _state)
{
    WasapiStreamValue state;
    memcpy(&state, _state, sizeof state);

    if (state.header.error != WasapiStreamError_Success) return;

    CloseHandle(state.refill_event);
    state.refill_event = 0;

    state.render_client->Release();
    state.render_client = nullptr;

    state.audio_client->Stop();
    state.audio_client->Release();
    state.audio_client = nullptr;

    state.header.error = WasapiStreamError_Closed;
    memcpy(_state, &state, sizeof state);
}


static WasapiBuffer
wasapi_buffer_acquire(WasapiStreamHeader* _state_header, WasapiStreamValue const& state, uint32_t max_buffer_frame_count)
{
    WasapiStreamHeader& state_header = *_state_header;
    WasapiBuffer result = {};

    if (state_header.error == WasapiStreamError_Success)
    {

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
    WasapiStreamValue state;
    memcpy(&state, _state, sizeof state);
    if (state.header.error != WasapiStreamError_Success) return {};

    auto time_out_ms = INFINITE;
    time_out_ms = 2*lrint(1000.0*double(max_frame_count)/double(state.audio_hz));
    auto wait_res = WaitForSingleObject(state.refill_event, time_out_ms);
    bool missed_deadline = wait_res == WAIT_TIMEOUT;

    // Detect potential device changes
    auto &state_header = state.header;
    const auto current_output_device_version =
        notification_client_get_default_device_version(
            win32_wasapi_devices.endpoint_notification_client);
    if (state.output_device_version != current_output_device_version)
    {
        FAIL_WITH("default device changed");
        memcpy(_state, &state, sizeof state);
        win32_wasapi_sound_close(_state);
        return {};
    }
    else if (missed_deadline)
    {
        FAIL_WITH("Unresponsive device");
        memcpy(_state, &state, sizeof state);
        win32_wasapi_sound_close(_state);
        return {};
    }
    return wasapi_buffer_acquire(&_state->header, state, max_frame_count);
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

struct WasapiEndpointNotificationClient : public IMMNotificationClient
{
    LONG reference_count = 0;
    LONG default_device_version = 0;
    IMMDeviceEnumerator* device_enumerator = nullptr;

    WasapiEndpointNotificationClient(IMMDeviceEnumerator* device_enumerator)
    {
        this->device_enumerator = device_enumerator;
        this->device_enumerator->AddRef();
        auto hr = this->device_enumerator->RegisterEndpointNotificationCallback(this);
        if (hr != S_OK)
        {
            this->device_enumerator->Release();
            this->device_enumerator = nullptr;
        }
    }

    ~WasapiEndpointNotificationClient()
    {
        if (this->device_enumerator)
        {
            this->device_enumerator->UnregisterEndpointNotificationCallback(this);
            this->device_enumerator->Release();
            this->device_enumerator = nullptr;
        }
    }

    // IUnknown:
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid, VOID **ppvInterface) override;

    // IMMNotificationClient:
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(
        EDataFlow flow,ERole role, LPCWSTR pwstrDeviceId) override;
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) override;
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) override;
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(
        LPCWSTR pwstrDeviceId,
        DWORD dwNewState) override;
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(
        LPCWSTR pwstrDeviceId,
        const PROPERTYKEY key) override;
};

ULONG WasapiEndpointNotificationClient::AddRef()
{
    return InterlockedIncrement(&reference_count);
}

ULONG WasapiEndpointNotificationClient::Release()
{
    ULONG res = InterlockedDecrement(&reference_count);
    if (0 == reference_count)
        delete this;
    return res;
}

HRESULT WasapiEndpointNotificationClient::QueryInterface(
REFIID riid, VOID **ppvInterface)
{
    if (IID_IUnknown == riid)
    {
        AddRef();
        *ppvInterface = (IUnknown*)this;
    }
    else if (__uuidof(IMMNotificationClient) == riid)
    {
        AddRef();
        *ppvInterface = (IMMNotificationClient*)this;
    }
    else
    {
        *ppvInterface = NULL;
        return E_NOINTERFACE;
    }
    return S_OK;
}

HRESULT WasapiEndpointNotificationClient::OnDefaultDeviceChanged(
EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId)
{
    if (role == eConsole && flow == eRender)
    {
        InterlockedIncrement(&default_device_version);
    }
    return S_OK;
}

HRESULT WasapiEndpointNotificationClient::OnDeviceAdded(LPCWSTR pwstrDeviceId)
{
    return S_OK;
}

HRESULT WasapiEndpointNotificationClient::OnDeviceRemoved(LPCWSTR pwstrDeviceId)
{
    return S_OK;
}

HRESULT WasapiEndpointNotificationClient::OnDeviceStateChanged(
LPCWSTR pwstrDeviceId,
DWORD dwNewState)
{
    return S_OK;
}

HRESULT WasapiEndpointNotificationClient::OnPropertyValueChanged(
LPCWSTR pwstrDeviceId,
const PROPERTYKEY key)
{
    return S_OK;
}

static WasapiEndpointNotificationClient* endpoint_notifications_acquire(IMMDeviceEnumerator* device_enumerator)
{
    auto notification_client = new WasapiEndpointNotificationClient(device_enumerator);
    device_enumerator->RegisterEndpointNotificationCallback(notification_client);
    notification_client->AddRef();
    return notification_client;
}

static void endpoint_notifications_release(WasapiEndpointNotificationClient* client)
{
    client->Release();
    if (client->reference_count != 0) cpu_debugbreak();
}


static uint32_t notification_client_get_default_device_version(WasapiEndpointNotificationClient* client)
{
    return client->default_device_version;
}


#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#if defined(win32_wasapi_own_cpu_debugbreak)
#undef cpu_debugbreak
#endif
#undef fatal_msg
#undef OS_SUCCESS
#undef THEN_DO
#undef FAIL_WITH
#undef RETURN_ON_ERROR
#undef RETURN_ON_ERROR_WITH

#if defined(WIN32_WASAPI_SOUND_EXAMPLE)

#include <cmath>

static constexpr double TAU = 6.2831853071795864769252;

static double
db_to_amp (double volume_in_db)
{
    return pow (exp (volume_in_db), log (10.0) / 20.0);
}

static void reference_tone_n(float* stereo_frames, int frame_count)
{
    static const auto reference_hz = 1000;
    static const auto reference_amp = db_to_amp(-20.0);
    static double phase;

    double phase_delta = reference_hz / 48000.0;
    for (int i = 0; i < frame_count; ++i) {
        float y = float(reference_amp * std::sin(TAU*phase));
        stereo_frames[2*i] = stereo_frames[2*i + 1] = y;
        phase += phase_delta;
    }
}

int main(int argc, char** argv)
{
    (void) argc; (void) argv;

    WasapiStream stream;
    auto audio_rate_hz = 48000;
    auto result = win32_wasapi_sound_open_stereo(&stream, audio_rate_hz);
    bool volatile is_running = result == WasapiStreamError_Success;
    uint64_t frame_count = 0;
    while (is_running)
    {
        auto buffer = win32_wasapi_sound_buffer_block_acquire(&stream, 4096);
        frame_count += buffer.frame_count;
        reference_tone_n((float*)buffer.bytes_first, buffer.frame_count);
        win32_wasapi_sound_buffer_release(&stream, buffer);
        if (frame_count > audio_rate_hz*60) {
            is_running = false;
        }
        if (stream.header.error == WasapiStreamError_Closed) {
            if (win32_wasapi_sound_open_stereo(&stream, audio_rate_hz)) {
                stream.header.error = WasapiStreamError_Closed;
            }
        }

    }
    win32_wasapi_sound_close(&stream);

    win32_wasapi_devices_release(&win32_wasapi_devices);
    CoUninitialize();
}
#endif