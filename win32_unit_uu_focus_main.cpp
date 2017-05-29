// @os: win32
#define UNICODE 1
#define _UNICODE 1

#define PLATFORM_NOTIFY_USE_MESSAGEBOX 0
#define PLATFORM_NOTIFY_USE_NOTIFICATION_AREA 1

#include "uu_focus_main.hpp"
#include "uu_focus_effects.hpp"
#include "uu_focus_effects_types.hpp"
#include "uu_focus_platform.hpp"

#include <sal.h>
#include <stdint.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <Commctrl.h>
#pragma comment(linker, "/MANIFESTDEPENDENCY:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <Shellapi.h>

#include "win32_wasapi_sound.hpp"

#include "win32_comctl32.hpp"
#include "win32_kernel32.hpp"
#include "win32_user32.hpp"
#include "win32_shell32.hpp"

#include <D2d1.h>
#pragma comment(lib, "d2d1.lib")

#include <dwrite.h>
#pragma comment(lib, "dwrite.lib")

// Taskbar:
// @url: https://msdn.microsoft.com/en-us/library/windows/desktop/dd378460(v=vs.85).aspx
//
// Notification area of windows:
// @url: https://msdn.microsoft.com/en-us/library/windows/desktop/ee330740(v=vs.85).aspx

static WIN32_WINDOW_PROC(main_window_proc);

static kernel32 modules_kernel32;
static user32 modules_user32;
static shell32 modules_shell32;
static comctl32 modules_comctl32;

static UUFocusMainCoroutine global_uu_focus_main;
static uint64_t global_qpf_hz;
static uint64_t global_qpc_origin;
static int32_t global_audio_thread_must_quit;
static WasapiStream global_sound;

static uint64_t now_micros();

static ID2D1Factory *global_d2d1factory;
static IDWriteFactory* global_dwritefactory;

struct Platform {
    HWND main_hwnd;
} global_platform;

static THREAD_PROC(audio_thread_main);

extern "C" int WINAPI WinMain(
    _In_ HINSTANCE hI,
    _In_opt_ HINSTANCE hPI,
    _In_ char* lpCmdLine,
    _In_ int nCmdShow)
{
    auto const kernel32 = LoadKernel32();
    modules_kernel32 = kernel32;

    /* set up performance counters */ {
        LARGE_INTEGER x;
        kernel32.QueryPerformanceFrequency(&x);
        global_qpf_hz = x.QuadPart;
        kernel32.QueryPerformanceCounter(&x);
        global_qpc_origin = x.QuadPart;
    }

    /* d2d1 */ {
        auto hr = D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory),
            /*D2D1_FACTORY_OPTIONS*/nullptr,
            reinterpret_cast<void**>(&global_d2d1factory));
        if (S_OK != hr) return 0x26'0b'6c'11; // "failed to create d2d1factory"
    }
    /* dwrite */ {
        auto hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&global_dwritefactory));
        if (S_OK != hr) return 0xca'8e'b3'08; // "failed to create dwrite factory"
    }

    auto &d2d1factory = *global_d2d1factory;
    FLOAT dpi_x;
    FLOAT dpi_y;
    d2d1factory.GetDesktopDpi(&dpi_x, &dpi_y);

    auto const user32 = LoadUser32(kernel32);
    modules_user32 = user32;
    WNDCLASSEXW main_class = {};
    {
        main_class.cbSize = sizeof(main_class);
        main_class.style = CS_VREDRAW | CS_HREDRAW;
        main_class.lpfnWndProc = main_window_proc;
        main_class.lpszClassName = L"uu_focus";
    }
    user32.RegisterClassExW(&main_class);
    auto main_hwnd = user32.CreateWindowExW(
        DWORD{0},
        main_class.lpszClassName,
        L"UUFocus",
        DWORD{WS_OVERLAPPEDWINDOW},
        int32_t(CW_USEDEFAULT),
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        HWND{0},
        HMENU{0},
        HINSTANCE{0},
        0);
    DWORD error = {};
    if (!main_hwnd) {
        error = kernel32.GetLastError();
        return error;
    }
    user32.ShowWindow(main_hwnd, nCmdShow);

    modules_shell32 = LoadShell32(kernel32);
    modules_comctl32 = LoadComctl32(kernel32);

    auto& sound = global_sound;
    win32_wasapi_sound_open_stereo(&sound, 48000); // TODO(nicolas): how about opening/closing on demand
    DWORD thread_id;
    kernel32.CreateThread(
        /* thread attributes */nullptr,
        /* default stack size */0,
        audio_thread_main,
        /* parameter*/ 0,
        /* creation state flag: start immediately */0,
        &thread_id);

    /* win32 message loop */ {
        MSG msg;
        while (user32.GetMessageW(&msg, HWND{0}, 0, 0)) {
            user32.TranslateMessage(&msg);
            user32.DispatchMessageW(&msg);
        }
    }

    global_audio_thread_must_quit = 1;
    win32_wasapi_sound_close(&sound);

    return error;
}

static uint64_t now_micros()
{
    LARGE_INTEGER pc;
    modules_kernel32.QueryPerformanceCounter(&pc);
    uint64_t y = pc.QuadPart - global_qpc_origin;
    y *= 1'000'000;
    y /= global_qpf_hz;
    return y;
}

static void d2d1_render(HWND hwnd);

static WIN32_WINDOW_PROC(main_window_proc)
{
    static const UINT_PTR refresh_timer_id = 1;

    auto &main = global_uu_focus_main;
    auto const& user32 = modules_user32;
    main.input.command = {};
    main.input.time_micros = now_micros();

    if (main.timer_effect) {
        main.timer_effect->now_micros = now_micros();
    }

    switch (uMsg) {
        case WM_CREATE: {
            // init
            global_platform.main_hwnd = hWnd;

            auto &main_state = global_uu_focus_main;
            main_state.timer_effect = timer_make(&global_platform);
            main_state.input.command = {};
            main_state.input.time_micros = now_micros();

            uu_focus_main(&main_state);
        } break;

        case WM_DESTROY: {
            user32.KillTimer(hWnd, refresh_timer_id);
            main.input.command.type = Command_application_stop;
            uu_focus_main(&main);
            user32.PostQuitMessage(0);
        } break;

        case WM_LBUTTONDOWN: {
            main.input.command.type = Command_timer_start;
            uu_focus_main(&main);
            auto timer_period_ms = 60;
            user32.SetTimer(hWnd, refresh_timer_id, timer_period_ms, NULL);
        } break;

        case WM_RBUTTONDOWN: {
            main.input.command.type = Command_timer_stop;
            uu_focus_main(&main);
        } break;

        case WM_PAINT: {
            d2d1_render(hWnd);
        } break;

        case WM_TIMER: {
            uu_focus_main(&main);
            if (!timer_is_active(main.timer_effect)) {
                user32.KillTimer(hWnd, refresh_timer_id);
            }
        } break;
    }
    return user32.DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

static void timer_render(TimerEffect const& timer, ID2D1HwndRenderTarget* rt);
static void welcome_render(ID2D1HwndRenderTarget* rt);

static void d2d1_render(HWND hwnd)
{
    static int count = 0;
    ++count; // paint once

    static ID2D1HwndRenderTarget* global_render_target;
    static HWND global_hwnd;
    static int global_client_width;
    static int global_client_height;

    auto& user32 = modules_user32;
    auto& d2d1factory = *global_d2d1factory;

    RECT rc;
    user32.GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    HRESULT hr = S_OK;

    if (!global_render_target ||
        hwnd != global_hwnd ||
        !(width == global_client_width && height == global_client_height)) {
        if (global_render_target) global_render_target->Release();
        hr = d2d1factory.CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(width, height)),
            &global_render_target);
        if (hr != S_OK) return;
        global_hwnd = hwnd;
        global_client_width = width;
        global_client_height = height;
    }

    if (S_OK != hr) return;
    auto &rt = *global_render_target;
    rt.BeginDraw();

    auto &main = global_uu_focus_main;
    if (!timer_is_active(main.timer_effect)) {
        rt.Clear(D2D1::ColorF(D2D1::ColorF::Black, 1.0f));
        welcome_render(&rt);
    } else /* timer is active */ {
        rt.Clear(D2D1::ColorF(D2D1::ColorF::Green, 1.0f));
        timer_render(*main.timer_effect, &rt);
    }
    hr = rt.EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        rt.Release();
        global_render_target = nullptr;
    }
}

static int digits_count(int32_t x)
{
    int digits = 0;
    for (; x; x /= 10) {
        ++digits;
    }
    if (!digits) digits = 1;
    return digits;
}

static char* string_push_n(char* dst_first,
                           char* dst_last,
                           char const* text,
                           size_t text_n)
{
    char* dst = dst_first;
    while (text_n && dst != dst_last) {
        *dst = *text;
        ++dst;
        ++text;
        --text_n;
    }
    return text_n == 0 ? dst : dst_first;
}


static char* string_push_zstring(char* dst_first,
                                 char* const dst_last,
                                 char const* zstring)
{
    auto dst = dst_first;
    while(*zstring && dst != dst_last) {
        *dst = *zstring;
        ++dst;
        ++zstring;
    }
    return *zstring ? dst_first : dst;
}

static char* string_push_i32(char* dst_first,
                             char* const dst_last,
                             int32_t const x,
                             int digits_min)
{
    auto x_digits = digits_count(x);
    auto const n = x_digits < digits_min ? digits_min : x_digits;
    if ((dst_last - dst_first) < n) return dst_first;

    int powers_rev[/*log10(1<<31): */ 10];
    /* powers */ {
        int *dst_rev = powers_rev;
        int y = 1;
        for (int i = 0; i < n; ++i) {
            dst_rev[n - 1 - i] = y;
            y *= 10;
        }
    }
    auto r = x;
    auto yr = 0;
    for (int i = 0; i < n; ++i) {
        r -= yr;
        auto y = r / powers_rev[i];
        dst_first[i] = '0' + y;
        yr = y * powers_rev[i];
    }
    return dst_first + n;
}

static void centered_text_render(ID2D1HwndRenderTarget* _rt, char* text_first, char* text_last)
{
    static IDWriteTextFormat *global_text_format;
    auto &dwrite = *global_dwritefactory;
    if (!global_text_format) {
        auto hr = dwrite.CreateTextFormat(
            /* font name */ L"Calibri",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            /* font size */ 34,
            /* locale */ L"",
            &global_text_format);
        // TODO(nicolas): hr ...
        if (S_OK != hr) return;
    }

    auto &rt = *_rt;
    auto &text_format = *global_text_format;
    auto size = rt.GetSize();

    text_format.SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    text_format.SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    enum { MAX_TEXT_SIZE = 100 };
    wchar_t render_text[MAX_TEXT_SIZE];
    auto render_text_last = render_text +
        MultiByteToWideChar(
            CP_UTF8,
            0,
            text_first,
            int(text_last - text_first),
            render_text,
            MAX_TEXT_SIZE);

    ID2D1SolidColorBrush* fg_brush;
    rt.CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.0f), &fg_brush);
    rt.DrawTextW(
        render_text,
        UINT32(render_text_last - render_text),
        &text_format,
        D2D1::RectF(0, 0, size.width, size.height),
        fg_brush);
    fg_brush->Release();
}

static void welcome_render(ID2D1HwndRenderTarget* _rt)
{
    enum { MAX_TEXT_SIZE = 100 };
    char text[MAX_TEXT_SIZE];
    auto const text_e = text + MAX_TEXT_SIZE;
    auto text_l = text;
    text_l = string_push_zstring(
        text_l, text_e, "Press LMB to start timer.");
    centered_text_render(_rt, text, text_l);

#if UU_FOCUS_INTERNAL
    /* path geometry test */ {
        auto &d2d1factory = *global_d2d1factory;
        ID2D1PathGeometry* _path_geometry;
        auto hr = d2d1factory.CreatePathGeometry(&_path_geometry);
        if (hr != S_OK) return;

        auto& pg = *_path_geometry;
        ID2D1GeometrySink* _path_geometry_sink;
        if (SUCCEEDED(pg.Open(&_path_geometry_sink))) {
            auto &sink = *_path_geometry_sink;
            sink.BeginFigure(D2D1::Point2F(0, 0),
                             D2D1_FIGURE_BEGIN_HOLLOW);
            sink.AddLine(D2D1::Point2F(100, 100));
            sink.EndFigure(D2D1_FIGURE_END_CLOSED);
            sink.Close();

            _path_geometry_sink->Release();
            _path_geometry_sink = nullptr;
        }

        auto& rt = *_rt;
        ID2D1SolidColorBrush* fg_brush;
        rt.CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.0f), &fg_brush);
        rt.DrawGeometry(_path_geometry, fg_brush);

        fg_brush->Release();

        _path_geometry->Release();
        _path_geometry = nullptr;
    }
#endif
}

static void timer_render(TimerEffect const& timer, ID2D1HwndRenderTarget* _rt)
{
    int const timer_countdown_s =
        1+int((timer.end_micros - now_micros())/1'000'000);

    enum { MAX_TEXT_SIZE = 100 };
    char text[MAX_TEXT_SIZE];
    auto text_end = text + MAX_TEXT_SIZE;
    auto text_last = text;
    /* build countdown text */ {
        auto const x_s = timer_countdown_s;
        auto const rl = text_end;
        auto rc = text_last;

        auto minutes = x_s / 60;
        auto seconds = x_s - minutes*60;

        rc = string_push_i32(rc, rl, minutes, 2);
        rc = string_push_n(rc, rl, ":", 1);
        rc = string_push_i32(rc, rl, seconds, 2);
        text_last = rc;
    }
    text_last = string_push_zstring(
        text_last, text_end,
        "\npress RMB to stop, LMB to reset.");

    centered_text_render(_rt, text, text_last);
}

#include <cassert>

void platform_render_async(Platform* _platform)
{
    auto& platform = *_platform;
    auto& user32 = modules_user32;
    auto hwnd = platform.main_hwnd;
    assert(hwnd != NULL);
    user32.InvalidateRect(hwnd, nullptr, FALSE);
}

struct UITextValue
{
    char const* utf8_data_first;
    int32_t utf8_data_size;
    enum : char { MemoryOwnership_Borrowed } ownership;
};
static_assert(sizeof (UITextValue) <= sizeof (UIText), "value must fit");

// TODO(nicolas): works only with literals for now
UIText ui_text(char const* zstr)
{
    UITextValue text;
    text.ownership = UITextValue::MemoryOwnership_Borrowed;
    text.utf8_data_first = zstr;
    int n = 0;
    while(*zstr++) ++n;
    text.utf8_data_size = n;

    UIText result;
    memcpy(&result, &text, sizeof text);
    return result;
}

#include <Strsafe.h>

void platform_notify(Platform* _platform, UIText _text)
{
    static wchar_t content_memory[1024];
    auto& platform = *_platform;
    auto& kernel32 = modules_kernel32;
    UITextValue text;
    memcpy(&text, &_text, sizeof text);
    auto content_first = content_memory;
    auto content_last = content_first +
        kernel32.MultiByteToWideChar(CP_UTF8, 0, text.utf8_data_first, text.utf8_data_size, content_memory, 1024);
    *content_last = '\0';

#if PLATFORM_NOTIFY_USE_MESSAGEBOX
    auto& user32 = modules_user32;
    user32.MessageBoxW(platform.main_hwnd, content_first, L"Notification", MB_OK);
#endif

#if PLATFORM_NOTIFY_USE_NOTIFICATION_AREA
    /* notification area */ {
        auto& shell32 = modules_shell32;
        auto &comctl32 = modules_comctl32;

        NOTIFYICONDATA nid = {};
        nid.cbSize = sizeof nid;
        nid.hWnd = platform.main_hwnd;
        nid.uFlags = NIF_ICON | NIF_TIP | NIF_GUID;
        nid.guidItem = {0x8a91e682, 0x35d3, 0x443e, { 0xa2, 0xda, 0x9f, 0x4a, 0x19, 0xfc, 0xe8, 0x66} };
        StringCchCopy(nid.szTip, ARRAYSIZE(nid.szTip), L"UUFocus");
        nid.uVersion = NOTIFYICON_VERSION_4;

        nid.uFlags |= NIF_INFO;
        StringCchCopy(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), L"UUFocus");
        StringCchCopy(nid.szInfo, ARRAYSIZE(nid.szInfo), content_first);
        nid.dwInfoFlags = NIIF_INFO|NIIF_RESPECT_QUIET_TIME;

        // Load the icon for high DPI.
        comctl32.LoadIconMetric(nullptr, IDI_INFORMATION, LIM_SMALL, &nid.hIcon);

        shell32.Shell_NotifyIconW(NIM_DELETE, &nid);
        shell32.Shell_NotifyIconW(NIM_ADD, &nid);
        shell32.Shell_NotifyIconW(NIM_SETVERSION, &nid);
        shell32.Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
#endif
}

static THREAD_PROC(audio_thread_main)
{
    while (!global_audio_thread_must_quit) {
        auto buffer = win32_wasapi_sound_buffer_block_acquire(&global_sound, 48000 / 60 + 2 * 48);
        audio_thread_render(
            nullptr,
            reinterpret_cast<float*>(buffer.bytes_first),
            buffer.frame_count);
        win32_wasapi_sound_buffer_release(&global_sound, buffer);
    }
    return 0;
}

#include "uu_focus_main.cpp"
#include "uu_focus_effects.cpp"

#include "win32_wasapi_sound.cpp"

#include "win32_comctl32.cpp"
#include "win32_user32.cpp"
#include "win32_shell32.cpp"
#include "win32_kernel32.cpp"
