// @language: c++14
// @os: win32
static const wchar_t* const global_application_name = L"UUFocus";
static bool global_uiautomation_on = true;

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

#include <uiautomation.h>
#pragma comment(lib, "uiautomationcore.lib")

// Taskbar:
// @url: https://msdn.microsoft.com/en-us/library/windows/desktop/dd378460(v=vs.85).aspx
//
// Notification area of windows:
// @url: https://msdn.microsoft.com/en-us/library/windows/desktop/ee330740(v=vs.85).aspx

static WIN32_WINDOW_PROC(main_window_proc);
static WIN32_WINDOW_PROC(child_window_proc);

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
UU_FOCUS_GLOBAL IUIAutomation* global_UIAutomation;

#if UU_FOCUS_INTERNAL

extern size_t global_palettes_n;
extern int global_palette_i;

#include "uu_focus_ui.hpp"

UU_FOCUS_GLOBAL win32_reloadable_modules::ReloadableModule global_ui_module;
typedef UU_FOCUS_RENDER_UI_PROC(UUFocusRenderUIProc);
UU_FOCUS_GLOBAL bool global_internal_ui_render_p;
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
    /* ui automation */ {
        CoInitialize(nullptr);
        auto hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                   __uuidof(IUIAutomation), (void**)&global_UIAutomation);
        if (S_OK != hr) return 0xf1'13'7b'ad; // "failed to obtain IUIAutomation"
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
        auto &d = main_class;
        d.cbSize = sizeof(d);
        d.style = CS_VREDRAW | CS_HREDRAW;
        d.lpfnWndProc = main_window_proc;
        d.lpszClassName = L"uu_focus";
        d.hIcon = (HICON)user32.LoadImageW(
            GetModuleHandle(nullptr),
            IDI_APPLICATION,
            IMAGE_ICON,
            0, 0, LR_DEFAULTSIZE);
        if (!d.hIcon) {
            return GetLastError();
        }
        win32_set_background(&d); // avoid flashing white at ShowWindow
    }
    user32.RegisterClassExW(&main_class);

    WNDCLASSEXW child_class = {};
    {
        auto &d = child_class;
        d.cbSize = sizeof(d);
        d.style = CS_HREDRAW | CS_VREDRAW;
        d.lpfnWndProc = child_window_proc;
        d.lpszClassName = L"uu_focus_child";
        win32_set_background(&d); // avoid flashing white at ShowWindow
    }
    user32.RegisterClassExW(&child_class);

    HWND main_hwnd = {};
    {
        DWORD window_style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        RECT window_rect = {};
        {
            window_rect.right = 600;
            window_rect.bottom = 200;
            user32.AdjustWindowRect(&window_rect, window_style, /*bMenu*/false);
        }
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
    modules_shell32 = LoadShell32(kernel32);
    modules_comctl32 = LoadComctl32(kernel32);

    auto& sound = global_sound;
    // NOTE(nicolas): dependency between the pink noise filter and 48khz
    win32_wasapi_sound_open_stereo(&sound, 48000); // TODO(uucidl): how about opening/closing on demand
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

struct Float3 { float x, y, z; };
struct Int2 { int x, y; };
struct IntBox2 { Int2 min, max; };

struct Ui
{
    double validity_ms; // time to live for the produced frame
    uint64_t validity_end_micros;
    // Unique component
    struct
    {
        char const* id;
        IntBox2 box;

        char const* opaque_layer_id;
        Float3 opaque_layer_background_color;

        char* centered_text;
        int centered_text_n;
    } components;

    char* text_data;
    int text_data_i;
    int text_data_n;
};

static void UiFree(Ui* ui_);

static IRawElementProviderSimple* ui_root_automation_provider_get(HWND);

static WIN32_WINDOW_PROC(main_window_proc)
{
    UU_FOCUS_FN_STATE HWND child_window;
    auto const& user32 = modules_user32;
    auto &main = global_uu_focus_main;

    switch (uMsg) {
        case WM_CREATE: {
            // init
            win32_platform_init(&global_platform, hWnd);

            RECT rc;
            user32.GetClientRect(hWnd, &rc);

            child_window = user32.CreateWindowExW(
                /* dwExStyle */ 0,
                L"uu_focus_child",
                L"Main",
                WS_CHILD | WS_VISIBLE,
                /* x */ 0,
                /* y */ 0,
                /* nWidth */ rc.right - rc.left,
                /* nHeight */ rc.bottom - rc.top,
                hWnd,
                /* hMenu */ NULL,
                /* hInstance */ NULL,
                /* lpParam */ NULL);
        } break;

        case WM_DESTROY: {
            main.input.command.type = Command_application_stop;
            uu_focus_main(&main);
            user32.PostQuitMessage(0);
        } break;

        case WM_ERASEBKGND: { return 0; } break;

        case WM_SIZE: {
            RECT rc;
            user32.GetClientRect(hWnd, &rc);
            user32.SetWindowPos(
                child_window, NULL, rc.left, rc.top, rc.right, rc.bottom,
                SWP_NOZORDER | SWP_NOACTIVATE);
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

static WIN32_WINDOW_PROC(child_window_proc)
{
    static const UINT_PTR refresh_timer_id = 1;
    auto const& user32 = modules_user32;
    auto &main = global_uu_focus_main;
    main.input.command = {};
    main.input.time_micros = now_micros();

    if (main.timer_effect) {
        main.timer_effect->now_micros = now_micros();
    }

    switch (uMsg)
    {
        case WM_CREATE: {
            auto &main_state = global_uu_focus_main;
            main_state.timer_effect = timer_make(&global_platform);
            main_state.input.command = {};
            main_state.input.time_micros = now_micros();

            uu_focus_main(&main_state);

#if UU_FOCUS_INTERNAL
            win32_reloadable_modules::make_in_path(&global_ui_module, "H:\\uu.focus\\builds", "uu_focus_ui");
#endif
        } break;

        case WM_ERASEBKGND: { return 0; } break;

        case WM_LBUTTONDOWN: {
            main.input.command.type = Command_timer_start;
            uu_focus_main(&main);
            auto timer_period_ms = 60;
        } break;

        case WM_PAINT: {
            Ui ui = {};
            ui.validity_ms = 1e6;
            d2d1_render(hWnd, &ui);
            ui.validity_ms = ui.validity_ms < 0.0? 0.0 : ui.validity_ms;
            if (refresh_timer_id != user32.SetTimer(hWnd, refresh_timer_id, UINT(ui.validity_ms), NULL)) {
                WIN32_ABORT("Could not SetTimer: %x", GetLastError());
            }
            UiFree(&ui);
        } break;

        case WM_RBUTTONDOWN: {
            main.input.command.type = Command_timer_stop;
            uu_focus_main(&main);
        } break;

        case WM_TIMER: {
            uu_focus_main(&main);
            if (!timer_is_active(main.timer_effect)) {
                user32.KillTimer(hWnd, refresh_timer_id);
            }
        } break;

        #if UU_FOCUS_INTERNAL
        case WM_KEYDOWN: {
            if (wParam == 0x49 /* I */) {
                bool &toggle = global_internal_ui_render_p;
                toggle = !toggle;
            } else if (wParam == VK_RIGHT) {
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

        static char const* msg;
        case WM_GETOBJECT: /* accessibility */ {
            const auto dwObjId = (DWORD) lParam;
            switch (dwObjId) {
                case UiaRootObjectId: /* asking for an IUIAutomationProvider */ {
                    msg = "Asking for IUIAutomationProvider for window\n";
                    if (global_uiautomation_on) {
                        auto rootProvider = ui_root_automation_provider_get(hWnd);
                        if (rootProvider) {
                            auto result = UiaReturnRawElementProvider(
                                hWnd, wParam, lParam, rootProvider);
                            rootProvider->Release();
                            return result;
                        }
                    }
                } break;
            }
        } break;
    }

    return user32.DefWindowProcW(hWnd, uMsg, wParam, lParam);
};


static void UiNewFrame(Ui*);
static void UiBeginRegion(Ui*, int width, int height, char const* id);
static void UiEndRegion(Ui*, char const* id);
static void UiPushCenteredText(Ui*, char const* utf8_string, char const* utf8_string_l, char const* id);
static void UiBeginOpaqueLayer(Ui* ui_, float r, float g, float b, char const* id);
static void UiEndOpaqueLayer(Ui* ui_, char const* id);

static void uu_focus_ui_render(IntBox2 Box, Ui*);

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

static void centered_text_render(ID2D1HwndRenderTarget* _rt, char const* text_first, char const* text_last);

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

    UiNewFrame(&ui);
    uu_focus_ui_render(IntBox2{{0,0}, {width,height}}, &ui);

    auto &rt = *global_render_target;
    rt.BeginDraw();
    {
        auto const& col = ui.components.opaque_layer_background_color;
        rt.Clear(D2D1::ColorF(col.x, col.y, col.z, 1.0f));
    }
    centered_text_render(&rt,
                         ui.components.centered_text,
                         ui.components.centered_text + ui.components.centered_text_n);

#if UU_FOCUS_INTERNAL
    if (global_uu_focus_ui_render && global_internal_ui_render_p) {
        global_uu_focus_ui_render(&rt, global_d2d1factory, global_dwritefactory);
        internal_ui_render(&rt);
        if (UiaClientsAreListening()) {
            /* are we connected to UI Automation? */
            auto t = "hello ui automation";
            auto t_l = t + strlen(t);
            centered_text_render(&rt, t, t_l);
        }
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


static void centered_text_render(ID2D1HwndRenderTarget* _rt, char const* text_first, char const* text_last)
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

static void welcome_render(Ui* ui)
{
    enum { MAX_TEXT_SIZE = 100 };
    char text[MAX_TEXT_SIZE];
    auto const text_end = text + MAX_TEXT_SIZE;
    auto text_last = text;
    text_last = string_push_zstring(
        text_last, text_end, global_ui_welcome_string);
    UiPushCenteredText(ui, text, text_last, "welcome");
}

static void timer_end_render(TimerEffect const& timer, Ui* ui)
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
    UiPushCenteredText(ui, text, text_last, "welcome");
}

static void timer_render(TimerEffect const& timer, Ui* ui_)
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

    UiPushCenteredText(&ui, text, text_last, "timer_msg");
}

static void uu_focus_ui_render(IntBox2 Box, Ui* ui_)
{
    auto& ui = *ui_;
    UiBeginRegion(&ui,
                  Box.max.x - Box.min.x,
                  Box.max.y - Box.min.y, "main frame");

    auto &main = global_uu_focus_main;
    auto palette_i = (global_palette_i + main.timer_elapsed_n)
        % global_palettes_n;

    if (!timer_is_active(main.timer_effect)) {
        auto bg_color = global_palettes[palette_i].bg_off;
        UiBeginOpaqueLayer(&ui, bg_color[0], bg_color[1], bg_color[2], "main-layer");
        if (main.timer_effect->start_time.hours == 0 && main.timer_effect->start_time.minutes == 0) {
          welcome_render(&ui);
        } else {
          timer_end_render(*main.timer_effect, &ui);
        }
        UiEndOpaqueLayer(&ui, "main-layer");

    } else /* timer is active */ {
        auto bg_color = global_palettes[palette_i].bg_on;
        UiBeginOpaqueLayer(&ui, bg_color[0], bg_color[1], bg_color[2], "main-layer");
        timer_render(*main.timer_effect, &ui);
        UiEndOpaqueLayer(&ui, "main-layer");
    }
    UiEndRegion(&ui, "main frame");
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

static void UiFree(Ui* ui_)
{
    auto &ui = *ui_;
    free(ui.text_data);
    ui = {};
}

static void UiNewFrame(Ui* ui_)
{
    auto &ui = *ui_;
    ui.components = {};
    ui.text_data_i = 0;
}

static void UiBeginRegion(Ui* ui_, int width, int height, char const* id)
{
    auto &d = ui_->components;
    assert(!d.id);
    d.id = id;
    d.box.min = { 0, 0 };
    d.box.max = { width, height };
}

static void UiEndRegion(Ui* ui_, char const* id)
{
    auto const &d = ui_->components;
    assert(0 == strcmp(d.id, id));
}

static void UiBeginOpaqueLayer(Ui* ui_, float r, float g, float b, char const* id)
{
    auto &d = ui_->components;
    assert(!d.opaque_layer_id);
    d.opaque_layer_background_color = Float3{r, g, b};
    d.opaque_layer_id = id;
}

static void UiEndOpaqueLayer(Ui* ui_, char const* id)
{
    auto const &d = ui_->components;
    assert(0 == strcmp(d.opaque_layer_id, id));
}

static void UiPushCenteredText(Ui* ui_, char const* utf8_string, char const* utf8_string_l, char const* id)
{
    auto &ui = *ui_;
    auto &d = ui_->components;
    int need_n = (int)(utf8_string_l - utf8_string);
    if (ui.text_data_i + need_n > ui.text_data_n) {
        ui.text_data = (char*)realloc(ui.text_data, 2*(ui.text_data_i + need_n));
    }
    auto const p = ui.text_data + ui.text_data_i;
    auto const p_n = need_n;
    memcpy(p, utf8_string, p_n);
    d.centered_text = p;
    d.centered_text_n = p_n;
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

// UI Automation & Accessibility
//
// @url: https://msdn.microsoft.com/en-us/library/windows/desktop/ee684009(v=vs.85).aspx
// @quote{
// Microsoft UI Automation is an accessibility framework that enables
// Windows applications to provide and consume programmatic information
// about user interfaces (UIs). It provides programmatic access to most
// UI elements on the desktop. It enables assistive technology products,
// such as screen readers, to provide information about the UI to end
// users and to manipulate the UI by means other than standard input.
// UI Automation also allows automated test scripts to interact with the UI.
// }

// UI Automation Provider
// @url: https://msdn.microsoft.com/en-us/library/windows/desktop/ee671596(v=vs.85).aspx
//
// Tree where elements have parents, siblings and children.
// Three such kinds (views) of trees are accessible: raw, control, content.
// An element is either a control or content.
//
// Window (Framework) > FragmentRoot .. Fragments
//

// TODO(uucidl): basic protection against bad arguments in API calls,
// like null pointers for return values

// TODO(uucidl): construct/update fragment tree from the `ui` function
// `d2d1_render` if and only if clients are listening.

template <typename T>
// T models IUnknown
struct com_shared
{
    T* raw_ptr;
    HANDLE debug_thread_id = 0;

    com_shared(com_shared const& other) : com_shared(other.raw_ptr) {}
    explicit com_shared(T* x) : raw_ptr(x) { raw_ptr->AddRef(); }
    ~com_shared() { raw_ptr->Release(); }

    T& operator* ()
    {
        HANDLE thread_id = GetCurrentThread();
        if (!debug_thread_id) {
            debug_thread_id = thread_id;
        } else {
            assert(debug_thread_id == thread_id);
        }
        return *raw_ptr;
    }

    T* operator-> ()
    {
        T& ref = **this;
        return &ref;
    }
};

// UI tree kept for use by UIAutomation
struct UIAutomationUI : public IUnknown
{
    LONG reference_count = 0;
    Ui ui = {};
    uint64_t ui_validity_micros = 0; // absolute time where the ui is invalid

    ~UIAutomationUI() { UiFree(&ui); }

    ULONG AddRef() override
    {
        return InterlockedIncrement(&reference_count);
    }

    ULONG Release() override
    {
        auto res = InterlockedDecrement(&reference_count);
        if (res == 0) delete this;
        return res;
    };

    HRESULT QueryInterface(REFIID riid, VOID **ppvInterface) override
    {
        *ppvInterface = NULL;
        return E_NOINTERFACE;
    }
};

// TODO(uucidl): synthesize com objects from a generic tree description. If
// HTML can do it, why can't we? There should be no application specific
// knowledge in these platform specific classes.

struct TextUIAutomationProvider :
  public IRawElementProviderSimple,
  public IRawElementProviderFragment
{
    LONG reference_count = 0;
    com_shared<IRawElementProviderFragment> parent_fragment_provider;
    com_shared<IRawElementProviderFragmentRoot> parent_fragment_root_provider;
    com_shared<UIAutomationUI> shared_ui_snapshot;

    TextUIAutomationProvider(com_shared<UIAutomationUI> snapshot,
                             com_shared<IRawElementProviderFragment> parent,
                             com_shared<IRawElementProviderFragmentRoot> parent_root)
        : shared_ui_snapshot(snapshot),
          parent_fragment_provider(parent),
          parent_fragment_root_provider(parent_root)
    {}

    ULONG AddRef() override
    {
        return InterlockedIncrement(&reference_count);
    }

    ULONG Release() override
    {
        auto res = InterlockedDecrement(&reference_count);
        if (res == 0) delete this;
        return res;
    };

    HRESULT QueryInterface(REFIID riid, VOID **ppvInterface) override
    {
        *ppvInterface = NULL;
        if (__uuidof(IRawElementProviderSimple) == riid)
        {
            AddRef();
            *ppvInterface = (IRawElementProviderSimple*)this;
        }
        else if (__uuidof(IRawElementProviderFragment) == riid)
        {
            AddRef();
            *ppvInterface = (IRawElementProviderFragment*)this;
        }
        if (!*ppvInterface) return E_NOINTERFACE;
        return S_OK;
    }

    // IRawElementProviderSimple:
    HRESULT get_ProviderOptions(ProviderOptions *pRetVal) override;
    HRESULT get_HostRawElementProvider(IRawElementProviderSimple **) override;
    HRESULT GetPatternProvider(
        PATTERNID patternId, IUnknown  **pRetVal) override;
    HRESULT GetPropertyValue(PROPERTYID,VARIANT *) override;

    // IRawElementProviderFragment:
    HRESULT GetEmbeddedFragmentRoots(SAFEARRAY **) override;
    HRESULT GetRuntimeId(SAFEARRAY **) override;
    HRESULT Navigate(NavigateDirection,IRawElementProviderFragment **) override;
    HRESULT SetFocus(void) override;

    HRESULT get_BoundingRectangle(UiaRect *) override;
    HRESULT get_FragmentRoot(IRawElementProviderFragmentRoot **) override;
};

HRESULT TextUIAutomationProvider::get_ProviderOptions(ProviderOptions *pRetVal)
{
    *pRetVal = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
    return S_OK;
}

HRESULT TextUIAutomationProvider::get_HostRawElementProvider(IRawElementProviderSimple **pRetVal)
{
    *pRetVal = NULL;
    return S_OK;
}

HRESULT TextUIAutomationProvider::GetPatternProvider(
  PATTERNID patternId,
  IUnknown  **pRetVal)
{
#if TODO_DISABLED
    // TODO(uucidl): that's strange no? How could text be invokable
    public IInvokeProvider

    if (UIA_InvokePatternId == patternId)
    {
        AddRef();
        *pRetVal = static_cast<IInvokeProvider*>(
            const_cast<TextUIAutomationProvider*>(this));
        return S_OK;
    }
#endif
    *pRetVal = nullptr;
    return S_OK;
}

static BSTR alloc_ole_string_from_utf8_small(char const *utf8_chars, int utf8_chars_n)
{
    enum { MAX_TEXT_SIZE = 100 };
    wchar_t result_text[MAX_TEXT_SIZE];
    auto const result_text_n =
        MultiByteToWideChar(
        CP_UTF8,
        0,
        utf8_chars,
        utf8_chars_n,
        result_text,
        MAX_TEXT_SIZE);
    return SysAllocStringLen(result_text, result_text_n);
}

#include <OleAuto.h>
#pragma comment(lib, "OleAut32.lib")

HRESULT TextUIAutomationProvider::GetPropertyValue(
  PROPERTYID propertyId,
  VARIANT *pRetVal)
{
    auto const& ui = shared_ui_snapshot->ui;

    pRetVal->vt = VT_EMPTY;
    if (propertyId == UIA_ControlTypePropertyId) {
        pRetVal->vt = VT_I4;
        pRetVal->lVal = UIA_TextControlTypeId;
    } else if (propertyId == UIA_NamePropertyId) {
        pRetVal->vt = VT_BSTR;
        pRetVal->bstrVal = alloc_ole_string_from_utf8_small(
            ui.components.centered_text, ui.components.centered_text_n);
    } else if (propertyId == UIA_AutomationIdPropertyId) {
        pRetVal->vt = VT_BSTR;
        pRetVal->bstrVal = SysAllocString(L"Text");
    } else if (propertyId == UIA_IsContentElementPropertyId) {
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = VARIANT_TRUE;
    } else if (propertyId == UIA_IsControlElementPropertyId) {
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = VARIANT_TRUE;
    } else if (propertyId == UIA_ProviderDescriptionPropertyId) {
        pRetVal->vt = VT_BSTR;
        pRetVal->bstrVal = SysAllocString(L"UU: Uia Text");
    } else if (propertyId == UIA_IsKeyboardFocusablePropertyId) {
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = VARIANT_FALSE;
    } else if (propertyId == UIA_HasKeyboardFocusPropertyId) {
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = VARIANT_FALSE;
    }

    return S_OK;
}


HRESULT TextUIAutomationProvider::GetEmbeddedFragmentRoots(SAFEARRAY **pArray)
{
    *pArray = NULL; // no other fragments contained
    return S_OK;
}

HRESULT TextUIAutomationProvider::GetRuntimeId(SAFEARRAY ** pArray)
{
    int rId[] = { UiaAppendRuntimeId, -1 };
    int rId_n = 2;
    SAFEARRAY* sa = SafeArrayCreateVector(VT_I4, 0, rId_n);
    LONG d_i = 0;
    SafeArrayPutElement(sa, &d_i, (void*)&rId[0]);
    ++d_i;
    SafeArrayPutElement(sa, &d_i, (void*)&rId[1]);
    ++d_i;
    *pArray = sa;
    return S_OK;
}

HRESULT TextUIAutomationProvider::Navigate(
  NavigateDirection direction,
  IRawElementProviderFragment **pRetVal)
{
    *pRetVal = nullptr;
    switch (direction)
    {
        case NavigateDirection_Parent: {
            *pRetVal = &(*parent_fragment_provider);
        } break;

        case NavigateDirection_NextSibling:
        case NavigateDirection_PreviousSibling:
        {
            // I have no siblings
        } break;

        case NavigateDirection_FirstChild:
        case NavigateDirection_LastChild:
        {
            // Nor children
        } break;
    }

    if (*pRetVal)
    {
        (*pRetVal)->AddRef();
    }
    return S_OK;
}

HRESULT TextUIAutomationProvider::SetFocus(void)
{
    return S_OK; // nothing to do
}

HRESULT TextUIAutomationProvider::get_BoundingRectangle(UiaRect *pRetVal)
{
    return (*parent_fragment_provider).get_BoundingRectangle(pRetVal);
}

HRESULT TextUIAutomationProvider::get_FragmentRoot(IRawElementProviderFragmentRoot **pRetVal)
{
    *pRetVal = &(*parent_fragment_root_provider);
    (*pRetVal)->AddRef();
    return S_OK;
}

struct RootUIAutomationProvider :
  public IRawElementProviderSimple,
  public IRawElementProviderFragment,
  public IRawElementProviderFragmentRoot
{
    LONG reference_count = 0;
    HWND hWnd = nullptr;
    com_shared<UIAutomationUI> shared_ui_snapshot;

    RootUIAutomationProvider(HWND hWnd)
        : hWnd(hWnd),
          shared_ui_snapshot(new UIAutomationUI) {}

    // IUnknown:
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid, VOID **ppvInterface) override;

    // IRawElementProviderSimple:
    HRESULT get_ProviderOptions(ProviderOptions *pRetVal) override;
    HRESULT get_HostRawElementProvider(IRawElementProviderSimple **) override;
    HRESULT GetPatternProvider(
        PATTERNID patternId, IUnknown  **pRetVal) override;
    HRESULT GetPropertyValue(PROPERTYID,VARIANT *) override;

    // IRawElementProviderFragment:
    HRESULT GetEmbeddedFragmentRoots(SAFEARRAY **) override;
    HRESULT GetRuntimeId(SAFEARRAY **) override;
    HRESULT Navigate(NavigateDirection,IRawElementProviderFragment **) override;
    HRESULT SetFocus(void) override;

    HRESULT get_BoundingRectangle(UiaRect *) override;
    HRESULT get_FragmentRoot(IRawElementProviderFragmentRoot **) override;

    // IRawElementProviderFragmentRoot:
    HRESULT ElementProviderFromPoint(double x, double y,
        IRawElementProviderFragment **pRetVal);
    HRESULT GetFocus(IRawElementProviderFragment **pRetVal);

};

ULONG RootUIAutomationProvider::AddRef()
{
    return InterlockedIncrement(&reference_count);
}

ULONG RootUIAutomationProvider::Release()
{
    auto res = InterlockedDecrement(&reference_count);
    if (res == 0) delete this;
    return res;
}

HRESULT RootUIAutomationProvider::QueryInterface(REFIID riid, VOID **ppvInterface)
{
    void *pInterface = nullptr;
    if (__uuidof(IRawElementProviderSimple) == riid) {
        pInterface = (IRawElementProviderSimple*)this;
    } else if (__uuidof(IRawElementProviderFragment) == riid) {
        pInterface = (IRawElementProviderFragment*)this;
    } else if (__uuidof(IRawElementProviderFragmentRoot) == riid) {
        pInterface = (IRawElementProviderFragmentRoot*)this;
    } else {
        *ppvInterface = nullptr;
        return E_NOINTERFACE;
    }

    *ppvInterface = pInterface;
    AddRef();
    return S_OK;
}

HRESULT RootUIAutomationProvider::get_ProviderOptions(ProviderOptions *pRetVal)
{
    *pRetVal = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
    return S_OK;
}

HRESULT RootUIAutomationProvider::get_HostRawElementProvider(IRawElementProviderSimple **pRetVal)
{
    return UiaHostProviderFromHwnd(hWnd, pRetVal);
}

HRESULT RootUIAutomationProvider::GetPatternProvider(
  PATTERNID patternId,
  IUnknown  **pRetVal)
{
    *pRetVal = nullptr; // the host provider will reply for us
    return S_OK;
}

#include <OleAuto.h>
#pragma comment(lib, "OleAut32.lib")

HRESULT RootUIAutomationProvider::GetPropertyValue(
  PROPERTYID propertyId,
  VARIANT *pRetVal)
{
    pRetVal->vt = VT_EMPTY;

    if (propertyId == UIA_NamePropertyId) {
        pRetVal->vt = VT_BSTR;
        pRetVal->bstrVal = SysAllocString(L"Main Pane");
    } else if (propertyId == UIA_ControlTypePropertyId) {
        pRetVal->vt = VT_I4;
        pRetVal->lVal = UIA_PaneControlTypeId;
    } else if (propertyId == UIA_AutomationIdPropertyId) {
        pRetVal->vt = VT_BSTR;
        pRetVal->bstrVal = SysAllocString(L"RootControl");
    } else if (propertyId == UIA_IsControlElementPropertyId) {
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = VARIANT_TRUE;
    } else if (propertyId == UIA_IsContentElementPropertyId) {
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = VARIANT_TRUE;
    } else if (propertyId == UIA_ProviderDescriptionPropertyId) {
        pRetVal->bstrVal = SysAllocString(L"UU: Uia Root");
        if (pRetVal->bstrVal != NULL)
        {
            pRetVal->vt = VT_BSTR;
        }
    } else if (propertyId == UIA_IsKeyboardFocusablePropertyId) {
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = VARIANT_FALSE;
    } else if (propertyId == UIA_HasKeyboardFocusPropertyId) {
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = VARIANT_FALSE;
    }
    return S_OK;
}


HRESULT RootUIAutomationProvider::GetEmbeddedFragmentRoots(SAFEARRAY **pArray)
{
    *pArray = NULL; // no other fragments contained
    return S_OK;
}

HRESULT RootUIAutomationProvider::GetRuntimeId(SAFEARRAY ** pArray)
{
    // we are not a child, so we can return an id of null
    *pArray = NULL;
    return S_OK;
}

HRESULT RootUIAutomationProvider::Navigate(
  NavigateDirection direction,
  IRawElementProviderFragment **pRetVal)
{
    auto const micros = now_micros();
    auto &ui_snapshot = *shared_ui_snapshot;
    if (micros >= ui_snapshot.ui_validity_micros)
    {
        auto const& user32 = modules_user32;
        RECT rc;
        user32.GetClientRect(hWnd, &rc);

        UiNewFrame(&ui_snapshot.ui);
        uu_focus_ui_render(IntBox2{{rc.left,rc.top}, {rc.right,rc.bottom}}, &ui_snapshot.ui);
        ui_snapshot.ui_validity_micros = micros + static_cast<uint64_t>(ui_snapshot.ui.validity_ms * 1000);
    }

    *pRetVal = nullptr;
    switch (direction)
    {
        case NavigateDirection_Parent: {
            // don't know my parent
        } break;

        case NavigateDirection_NextSibling:
        case NavigateDirection_PreviousSibling:
        {
            // I have no siblings
        } break;

        case NavigateDirection_FirstChild:
        case NavigateDirection_LastChild:
        {
            *pRetVal = new TextUIAutomationProvider(
                shared_ui_snapshot,
                com_shared<IRawElementProviderFragment>(this),
                com_shared<IRawElementProviderFragmentRoot>(this));
            (*pRetVal)->AddRef();
        } break;
    }
    return S_OK;
}

HRESULT RootUIAutomationProvider::SetFocus(void)
{
    return UIA_E_INVALIDOPERATION;
}

HRESULT RootUIAutomationProvider::get_BoundingRectangle(UiaRect *pRetVal)
{
    UiaRect NullRect{};
    *pRetVal = NullRect; // get it from our host provider!
    return S_OK;
}

HRESULT RootUIAutomationProvider::get_FragmentRoot(IRawElementProviderFragmentRoot **pRetVal)
{
    *pRetVal = this;
    AddRef();
    return S_OK;
}

HRESULT RootUIAutomationProvider::ElementProviderFromPoint(double x, double y,
                                 IRawElementProviderFragment **pRetVal)
{
    // TODO(uucidl): @copypaste
    *pRetVal = new TextUIAutomationProvider(
        shared_ui_snapshot,
        com_shared<IRawElementProviderFragment>(this),
        com_shared<IRawElementProviderFragmentRoot>(this));
    (*pRetVal)->AddRef();
    return S_OK;
}

HRESULT RootUIAutomationProvider::GetFocus(IRawElementProviderFragment **pRetVal)
{
    // TODO(uucidl): @copypaste
    *pRetVal = NULL;
    return S_OK;
}


static IRawElementProviderSimple* ui_root_automation_provider_get(HWND hWnd)
{
    auto root_provider = new RootUIAutomationProvider(hWnd);
    root_provider->AddRef();
    return root_provider;
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
// TODO(uucidl): create a document child window, seems to be the only way to set our UiAutomationProviders correctly
// @url: https://github.com/Microsoft/Windows-classic-samples/blob/master/Samples/UIAutomationDocumentProvider/cpp/UiaDocumentProvider.cpp
