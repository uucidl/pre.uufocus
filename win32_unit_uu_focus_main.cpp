// @language: c++14
// @os: win32
static const wchar_t* const global_application_name = L"UUFocus";

// Configuration:
#define PLATFORM_NOTIFY_USE_MESSAGEBOX 0
#define PLATFORM_NOTIFY_USE_NOTIFICATION_AREA 1

//  Target Win7 and beyond.
#define WINVER 0x0601
#define _WIN32_WINNT 0x0601

#define UNICODE 1
#define _UNICODE 1
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#define UU_FOCUS_GLOBAL static
#define UU_FOCUS_FN_STATE static

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
#if UU_FOCUS_INTERNAL
#include "win32_reloadable_modules.hpp"
#endif

#include "win32_utils.ipp"

#include "win32_comctl32.hpp"
#include "win32_gdi32.hpp"
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
static uint64_t now_micros();

UU_FOCUS_GLOBAL kernel32 modules_kernel32;
UU_FOCUS_GLOBAL user32 modules_user32;
UU_FOCUS_GLOBAL shell32 modules_shell32;
UU_FOCUS_GLOBAL comctl32 modules_comctl32;
UU_FOCUS_GLOBAL gdi32 modules_gdi32;

UU_FOCUS_GLOBAL UUFocusMainCoroutine global_uu_focus_main;
UU_FOCUS_GLOBAL uint64_t global_qpf_hz;
UU_FOCUS_GLOBAL uint64_t global_qpc_origin;

UU_FOCUS_GLOBAL HANDLE global_sound_thread;
UU_FOCUS_GLOBAL int32_t global_sound_thread_must_quit;
UU_FOCUS_GLOBAL WasapiStream global_sound;

UU_FOCUS_GLOBAL ID2D1Factory *global_d2d1factory;
UU_FOCUS_GLOBAL IDWriteFactory* global_dwritefactory;

#if UU_FOCUS_INTERNAL

extern size_t global_palettes_n;
extern int global_palette_i;

#include "uu_focus_ui.hpp"

UU_FOCUS_GLOBAL win32_reloadable_modules::ReloadableModule global_ui_module;
typedef UU_FOCUS_RENDER_UI_PROC(UUFocusRenderUIProc);
UU_FOCUS_GLOBAL UUFocusRenderUIProc *global_uu_focus_ui_render;
#endif

struct Platform {
    HWND main_hwnd;
} global_platform;

static THREAD_PROC(audio_thread_main);

static void win32_platform_init(struct Platform*, HWND);
static void win32_platform_shutdown(struct Platform*);
static void win32_set_background(WNDCLASSEX* wndclass);
static void win32_abort_with_message(char const* pattern, ...);
#define WIN32_ABORT(...) __debugbreak(); win32_abort_with_message(__VA_ARGS__)

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
    auto const gdi32 = LoadGdi32(kernel32);
    modules_gdi32 = gdi32;

    WNDCLASSEXW main_class = {};
    {
        main_class.cbSize = sizeof(main_class);
        main_class.style = CS_VREDRAW | CS_HREDRAW;
        main_class.lpfnWndProc = main_window_proc;
        main_class.lpszClassName = L"uu_focus";
        main_class.hIcon = (HICON)user32.LoadImageW(
            GetModuleHandle(nullptr),
            IDI_APPLICATION,
            IMAGE_ICON,
            0, 0, LR_DEFAULTSIZE);
        if (!main_class.hIcon) {
            return GetLastError();
        }
        win32_set_background(&main_class); // avoid flashing white at ShowWindow
    }
    user32.RegisterClassExW(&main_class);
    HWND main_hwnd = {};
    {
        DWORD window_style = WS_OVERLAPPEDWINDOW;
        RECT window_rect = {};
        window_rect.right = 600;
        window_rect.bottom = 200;
        user32.AdjustWindowRect(&window_rect, window_style, /*bMenu*/false);
        main_hwnd = user32.CreateWindowExW(
            DWORD{0},
            main_class.lpszClassName,
            global_application_name,
            window_style,
            /* x */ int32_t(CW_USEDEFAULT),
            /* y */ CW_USEDEFAULT,
            /* nWidth */ window_rect.right - window_rect.left,
            /* nHeight */ window_rect.bottom - window_rect.top,
            /* hWwndParent */ HWND{0},
            HMENU{0},
            HINSTANCE{0},
            0);
    }
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
    if (sound.header.error == WasapiStreamError_Success)
    {
        global_sound_thread = kernel32.CreateThread(
            /* thread attributes */nullptr,
            /* default stack size */0,
            audio_thread_main,
            /* parameter*/ 0,
            /* creation state flag: start immediately */0,
            nullptr);
    }

    /* win32 message loop */ {
        MSG msg;
        while (user32.GetMessageW(&msg, HWND{0}, 0, 0)) {
            user32.TranslateMessage(&msg);
            user32.DispatchMessageW(&msg);
        }
    }

    global_sound_thread_must_quit = 1;
    if (WaitForSingleObject(global_sound_thread, INFINITE) != WAIT_OBJECT_0) {
        error = kernel32.GetLastError();
        return error;
    }
    win32_wasapi_sound_close(&sound);

    win32_platform_shutdown(&global_platform);

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

struct Ui;
static void d2d1_render(HWND hwnd, Ui*);

struct Ui
{
    double validity_ms; // time to live for the produced frame
    uint64_t validity_end_micros;
};

static WIN32_WINDOW_PROC(main_window_proc)
{
    UU_FOCUS_FN_STATE const UINT_PTR refresh_timer_id = 1;
    UU_FOCUS_FN_STATE Ui ui;

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
            win32_platform_init(&global_platform, hWnd);

            auto &main_state = global_uu_focus_main;
            main_state.timer_effect = timer_make(&global_platform);
            main_state.input.command = {};
            main_state.input.time_micros = now_micros();

            uu_focus_main(&main_state);

#if UU_FOCUS_INTERNAL
            win32_reloadable_modules::make_in_path(&global_ui_module, "H:\\uu.focus\\builds", "uu_focus_ui");
#endif
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
        } break;

        case WM_RBUTTONDOWN: {
            main.input.command.type = Command_timer_stop;
            uu_focus_main(&main);
        } break;

#if UU_FOCUS_INTERNAL
        case WM_KEYDOWN: {
            if (wParam == VK_RIGHT) {
                global_palette_i = (global_palette_i + 1) % global_palettes_n;
            } else {
                global_audio_mode = (global_audio_mode + 1) % global_audio_mode_mod;
            }
            platform_render_async(&global_platform);
        } break;

        case WM_MOUSEWHEEL: {
            auto zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            double delta = double(zDelta) / WHEEL_DELTA;
            double total_increments = global_separation_ms_max - global_separation_ms_min;
            double y_ms = global_separation_ms;
            y_ms += delta/total_increments;
            if (y_ms > global_separation_ms_max) y_ms = global_separation_ms_max;
            if (y_ms < global_separation_ms_min) y_ms = global_separation_ms_min;
            global_separation_ms = y_ms;
            platform_render_async(&global_platform);
        } break;
#endif

        case WM_PAINT: {
            ui.validity_ms = 1e6;
            d2d1_render(hWnd, &ui);
            ui.validity_ms = ui.validity_ms < 0.0? 0.0 : ui.validity_ms;
            ui.validity_end_micros = now_micros() + uint64_t(1000*ui.validity_ms);
            if (refresh_timer_id != user32.SetTimer(hWnd, refresh_timer_id, UINT(ui.validity_ms), NULL)) {
              WIN32_ABORT("Could not SetTimer: %x", GetLastError());
            }
        } break;

        case WM_TIMER: {
            uu_focus_main(&main);
            if (!timer_is_active(main.timer_effect)) {
                user32.KillTimer(hWnd, refresh_timer_id);
            }
            if (now_micros() > ui.validity_end_micros) {
                platform_render_async(&global_platform);
            }
        } break;
    }
#if UU_FOCUS_INTERNAL
    if (win32_reloadable_modules::has_changed(&global_ui_module)) {
        auto reload_attempt = load(&global_ui_module);
        address_assign(&global_uu_focus_ui_render, (HMODULE)global_ui_module.dll,
                       "win32_uu_focus_ui_render");
        platform_render_async(&global_platform);
    }
#endif
    return user32.DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

struct Ui;
static void welcome_render(ID2D1HwndRenderTarget* rt);
static void timer_render(TimerEffect const& timer, ID2D1HwndRenderTarget* rt, Ui*);
static void timer_end_render(TimerEffect const& timer, ID2D1HwndRenderTarget* rt_, Ui* ui_);

#if UU_FOCUS_INTERNAL
static void internal_ui_render(ID2D1HwndRenderTarget* rt);
#endif

UU_FOCUS_GLOBAL struct {
    float bg_off[3];
    float bg_on[3];
} global_palettes[] = {
    {
        { 110.0f/255.0f, 154.0f/255.0f, 201.0f/255.0f },
        { 166.0f/255.0f, 200.0f/255.0f, 235.0f/255.0f },
    },
    {
        { 153.0f/255.0f, 140.0f/255.0f, 131.0f/255.0f },
        { 218.0f/255.0f, 204.0f/255.0f, 193.0f/255.0f },
    },
    {
        {  44.0f/255.0f,  60.0f/255.0f,  13.0f/255.0f },
        { 165.0f/255.0f, 131.0f/255.0f,  85.0f/255.0f },
    },
    {
        {  90.0f/255.0f, 103.0f/255.0f,  50.0f/255.0f },
        { 182.0f/255.0f, 170.0f/255.0f, 154.0f/255.0f },
    },
};
UU_FOCUS_GLOBAL size_t global_palettes_n = sizeof global_palettes / sizeof *global_palettes;
UU_FOCUS_GLOBAL int global_palette_i = 0;

static void win32_set_background(WNDCLASSEX* window_class_)
{
    auto &window_class = *window_class_;
    auto const& gdi32 = modules_gdi32;
    auto const& bg = global_palettes[0].bg_on;
    // NOTE(nil): @leak we don't care, this is done once per app.
    auto bgbrush = gdi32.CreateSolidBrush(RGB(bg[0], bg[1], bg[2]));
    window_class.hbrBackground = bgbrush;
}

static void d2d1_render(HWND hwnd, Ui* ui_)
{
    UU_FOCUS_FN_STATE HWND global_hwnd;
    UU_FOCUS_FN_STATE int global_client_width;
    UU_FOCUS_FN_STATE int global_client_height;
    UU_FOCUS_FN_STATE ID2D1HwndRenderTarget* global_render_target;

    auto& ui = *ui_;
    auto const& user32 = modules_user32;
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

    auto &rt = *global_render_target;
    rt.BeginDraw();

    auto &main = global_uu_focus_main;
    auto palette_i = (global_palette_i + main.timer_elapsed_n)
        % global_palettes_n;

    if (!timer_is_active(main.timer_effect)) {
        auto bg_color = global_palettes[palette_i].bg_off;
        rt.Clear(D2D1::ColorF(bg_color[0], bg_color[1], bg_color[2], 1.0f));
        if (main.timer_effect->start_time.hours == 0 && main.timer_effect->start_time.minutes == 0) {
          welcome_render(&rt);
        } else {
          timer_end_render(*main.timer_effect, &rt, ui_);
        }
    } else /* timer is active */ {
        auto bg_color = global_palettes[palette_i].bg_on;
        rt.Clear(D2D1::ColorF(bg_color[0], bg_color[1], bg_color[2], 1.0f));
        timer_render(*main.timer_effect, &rt, ui_);
    }

#if UU_FOCUS_INTERNAL
    if (global_uu_focus_ui_render) {
        global_uu_focus_ui_render(&rt, global_d2d1factory, global_dwritefactory);
        internal_ui_render(&rt);
    }
#endif

    hr = rt.EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        rt.Release();
        global_render_target = nullptr;
        ui.validity_ms = 0;
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

#include <cstdio>

static char* string_push_double(char* dst_first,
                                char* const dst_last,
                                double x)
{
    auto res = std::snprintf(dst_first, dst_last - dst_first, "%f", x);
    if (res < 0) {
        return dst_first;
    }
    return dst_first + res;
}

static constexpr const auto global_ui_welcome_string = "Press LMB to start timer.";


static void centered_text_render(ID2D1HwndRenderTarget* _rt, char* text_first, char* text_last)
{
    UU_FOCUS_FN_STATE IDWriteTextFormat *global_text_format;
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
    rt.SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
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
    auto const text_end = text + MAX_TEXT_SIZE;
    auto text_last = text;
    text_last = string_push_zstring(
        text_last, text_end, global_ui_welcome_string);
    centered_text_render(_rt, text, text_last);
}

static void timer_end_render(TimerEffect const& timer, ID2D1HwndRenderTarget* rt_, Ui* ui_)
{
    enum { MAX_TEXT_SIZE = 100 };
    char text[MAX_TEXT_SIZE];
    auto text_end = text + MAX_TEXT_SIZE;
    auto text_last = text;

    text_last = string_push_zstring(
        text_last, text_end, global_ui_welcome_string);

    /* tell when the last cycle started */ {
      auto rc = text_last;
      auto const rl = text_end;
      rc = string_push_zstring(rc, rl, "\nLast cycle started at ");
      rc = string_push_i32(rc, rl, timer.start_time.hours, 2);
      rc = string_push_n(rc, rl, ":", 1);
      rc = string_push_i32(rc, rl, timer.start_time.minutes, 2);
      rc = string_push_zstring(rc, rl, ".");

      text_last = rc;
    }
    centered_text_render(rt_, text, text_last);
}

static void timer_render(TimerEffect const& timer, ID2D1HwndRenderTarget* rt_, Ui* ui_)
{
    auto &ui = *ui_;

    uint64_t end_from_now_micros = timer.end_micros - now_micros();
    uint64_t end_from_now_rounded_micros = (end_from_now_micros/1'000'000) * 1'000'000;
    uint64_t next_second_from_now_micros = end_from_now_micros - end_from_now_rounded_micros;

    int const timer_countdown_s = 1+int(end_from_now_rounded_micros/1'000'000);
    auto validity_ms = double(next_second_from_now_micros)*1e-3;
    ui.validity_ms = validity_ms<ui.validity_ms? validity_ms : ui.validity_ms;


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
        "\nPress RMB to stop, LMB to reset.");

    /* tell when the cycle started */ {
      auto rc = text_last;
      auto const rl = text_end;
      rc = string_push_zstring(rc, rl, "\nCycle started at ");
      rc = string_push_i32(rc, rl, timer.start_time.hours, 2);
      rc = string_push_n(rc, rl, ":", 1);
      rc = string_push_i32(rc, rl, timer.start_time.minutes, 2);
      rc = string_push_zstring(rc, rl, ".");

      text_last = rc;
    }

    centered_text_render(rt_, text, text_last);
}

#if UU_FOCUS_INTERNAL
static void internal_ui_render(ID2D1HwndRenderTarget* rt_)
{
    enum { MAX_TEXT_SIZE = 100 };
    char text[MAX_TEXT_SIZE];
    auto text_first = text;
    auto text_end = text + MAX_TEXT_SIZE;
    auto text_last = text;
    text_last = string_push_zstring(text_last, text_end, "Audio Separation: ");
    text_last = string_push_double(text_last, text_end, global_separation_ms);
    text_last = string_push_zstring(text_last, text_end, "ms");

    char text2[MAX_TEXT_SIZE];
    auto text2_first = text2;
    auto text2_end = text2 + MAX_TEXT_SIZE;
    auto text2_last = text2;
    text2_last = string_push_zstring(text2_last, text2_end, "Audio Mode: ");
    text2_last = string_push_i32(text2_last, text2_end, global_audio_mode, 2);

    UU_FOCUS_FN_STATE IDWriteTextFormat *global_text_format;
    auto &dwrite = *global_dwritefactory;
    float font_height_px = 17;
    if (!global_text_format) {
        auto hr = dwrite.CreateTextFormat(
            /* font name */ L"Calibri",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            font_height_px,
            /* locale */ L"",
            &global_text_format);
        if (S_OK != hr) return;
    }

    auto &rt = *rt_;
    auto &text_format = *global_text_format;
    auto size = rt.GetSize();

    text_format.SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    struct { char* text_f; __int64 text_n; } lines[] = {
        { text_first, text_last - text_first },
        { text2_first, text2_last - text2_first },
    };

    float y = 0.0;
    for (auto const& line : lines) {
        wchar_t render_text[MAX_TEXT_SIZE];
        auto render_text_last = render_text +
            MultiByteToWideChar(
            CP_UTF8,
            0,
            line.text_f,
            int(line.text_n),
            render_text,
            MAX_TEXT_SIZE);

        ID2D1SolidColorBrush* fg_brush;
        rt.CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.0f), &fg_brush);
        rt.DrawTextW(
            render_text,
            UINT32(render_text_last - render_text),
            &text_format,
            D2D1::RectF(0, y, size.width, font_height_px),
            fg_brush);
        y += font_height_px;
        fg_brush->Release();
    }
}
#endif


#include <cassert>

void platform_render_async(Platform* _platform)
{
    auto& platform = *_platform;
    auto& user32 = modules_user32;
    auto hwnd = platform.main_hwnd;
    assert(hwnd != NULL);
    user32.InvalidateRect(hwnd, nullptr, FALSE);
}

#include "uu_focus_platform_types.hpp"
#include <Strsafe.h>

static void win32_notifyicon_make(NOTIFYICONDATA* nid_, Platform const& platform)
{
    NOTIFYICONDATA& nid = *nid_;
    nid = {};
    nid.cbSize = sizeof nid;
    nid.hWnd = platform.main_hwnd;
    // NOTE(uucidl): we decided against using the guid, as it disallows sharing the notification with multiple executables.. Once the GUID is registered, you cannot move/rename etc.. the executable.
    // @url: https://blogs.msdn.microsoft.com/asklar/2012/03/06/system-tray-notification-area-icons/
    if (false) {
        nid.guidItem = {0x8a91e682, 0x35d3, 0x443e, { 0xa2, 0xda, 0x9f, 0x4a, 0x19, 0xfc, 0xe8, 0x66} };
        nid.uFlags |= NIF_GUID;
    } else {
        nid.uID = 0x8a91e682;
    }
    nid.uVersion = NOTIFYICON_VERSION_4;
}

void platform_notify(Platform* _platform, UIText _text)
{
    wchar_t content_memory[1024];
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

        NOTIFYICONDATA nid;
        win32_notifyicon_make(&nid, platform);
        auto name = global_application_name;
        StringCchCopy(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), name);
        nid.uFlags |= NIF_INFO;
        StringCchCopy(nid.szTip, ARRAYSIZE(nid.szTip), name);
        nid.uFlags |= NIF_TIP;
        StringCchCopy(nid.szInfo, ARRAYSIZE(nid.szInfo), content_first);
        nid.dwInfoFlags = NIIF_INFO|NIIF_RESPECT_QUIET_TIME;

        // Load the icon for high DPI.
        // comctl32.LoadIconMetric(nullptr, IDI_INFORMATION, LIM_SMALL, &nid.hIcon);
        if (S_OK ==
            comctl32.LoadIconMetric(GetModuleHandleW(nullptr),
                                    IDI_APPLICATION, LIM_SMALL, &nid.hIcon)) {
            nid.uFlags |= NIF_ICON;
        }

        shell32.Shell_NotifyIconW(NIM_ADD, &nid);
        shell32.Shell_NotifyIconW(NIM_SETVERSION, &nid);
        shell32.Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
#endif
}

Civil_Time_Of_Day platform_get_time_of_day()
{
  SYSTEMTIME win32_time;
  modules_kernel32.GetLocalTime(&win32_time);
  Civil_Time_Of_Day time = {};
  time.hh = win32_time.wHour;
  time.mm = win32_time.wMinute;
  return time;
}


static void win32_platform_init(struct Platform* platform_, HWND hWnd)
{
    auto& platform = *platform_;
    platform.main_hwnd = hWnd;
}

static void win32_platform_shutdown(struct Platform* platform_)
{
    auto const& shell32 = modules_shell32;
    auto& platform = *platform_;
    NOTIFYICONDATA nid;
    win32_notifyicon_make(&nid, platform);
    shell32.Shell_NotifyIconW(NIM_DELETE, &nid);
}

static THREAD_PROC(audio_thread_main)
{
    while (!global_sound_thread_must_quit) {
        auto buffer = win32_wasapi_sound_buffer_block_acquire(&global_sound, 48000 / 60 + 2 * 48);
        audio_thread_render(
            nullptr,
            reinterpret_cast<float*>(buffer.bytes_first),
            buffer.frame_count);
        win32_wasapi_sound_buffer_release(&global_sound, buffer);
        if (global_sound.header.error == WasapiStreamError_Closed) {
            if (win32_wasapi_sound_open_stereo(&global_sound, 48000)) {
                global_sound.header.error = WasapiStreamError_Closed;
            }
        }
    }
    return 0;
}

#include <cstdarg>
#include <cstdlib>

static void win32_abort_with_message(char const* pattern, ...)
{
    const auto& kernel32 = modules_kernel32;
    const auto& user32 = modules_user32;
    va_list arglist;
    va_start(arglist, pattern);
    char buffer[4096];
    auto buffer_n = std::vsnprintf(buffer, sizeof buffer, pattern, arglist);
    if (buffer_n >= 0) {
      wchar_t wbuffer[4096];
      auto wbuffer_last = wbuffer +
        kernel32.MultiByteToWideChar(CP_UTF8, 0, buffer, buffer_n, wbuffer, 4096);
      *wbuffer_last = '\0';
      user32.MessageBoxW(global_platform.main_hwnd, wbuffer, L"Defect", MB_OK);
      std::abort();
    }
}

#include "uu_focus_main.cpp"
#include "uu_focus_effects.cpp"
#include "uu_focus_platform.cpp"

#include "win32_wasapi_sound.cpp"

#include "win32_comctl32.cpp"
#include "win32_user32.cpp"
#include "win32_shell32.cpp"
#include "win32_gdi32.cpp"
#include "win32_kernel32.cpp"

// TODO(uucidl): DPI-awareness