// @os: win32

#include "uu_focus_main.hpp"
#include "uu_focus_effects.hpp"
#include "uu_focus_effects_types.hpp"
#include "uu_focus_platform.hpp"

#include <sal.h>
#include <stdint.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "win32_kernel32.hpp"
#include "win32_user32.hpp"

#include <d2d1.h>
#pragma comment(lib, "d2d1.lib")

// Taskbar:
// @url: https://msdn.microsoft.com/en-us/library/windows/desktop/dd378460(v=vs.85).aspx
//
// Notification area of windows:
// @url: https://msdn.microsoft.com/en-us/library/windows/desktop/ee330740(v=vs.85).aspx

static WIN32_WINDOW_PROC(main_window_proc);

static kernel32 global_kernel32;
static user32 global_user32;
static UUFocusMainCoroutine global_uu_focus_main;
static uint64_t global_qpf_hz;
static uint64_t global_qpc_origin;

static uint64_t now_micros();

static ID2D1Factory *global_d2d1factory;

struct Platform {
    HWND main_hwnd;
} global_platform;

extern "C" int WINAPI WinMain(
    _In_ HINSTANCE hI,
    _In_opt_ HINSTANCE hPI,
    _In_ char* lpCmdLine,
    _In_ int nCmdShow)
{
    auto const kernel32 = LoadKernel32();
    global_kernel32 = kernel32;

    /* set up performance counters */ {
        LARGE_INTEGER x;
        kernel32.QueryPerformanceFrequency(&x);
        global_qpf_hz = x.QuadPart;
        kernel32.QueryPerformanceCounter(&x);
        global_qpc_origin = x.QuadPart;
    }

    /* d2d1 */ {
        auto m = kernel32.LoadLibraryA("d2d1.dll");
        if (!m) {
            return 0x9f'00'36'6d; // "failed to load d2d1"
        }
        auto last_hresult = D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory),
            /*D2D1_FACTORY_OPTIONS*/nullptr,
            reinterpret_cast<void**>(&global_d2d1factory));
        if (S_OK != last_hresult) return 1;
    }

    auto &d2d1factory = *global_d2d1factory;
    FLOAT dpi_x;
    FLOAT dpi_y;
    d2d1factory.GetDesktopDpi(&dpi_x, &dpi_y);

    auto const user32 = LoadUser32(kernel32);
    global_user32 = user32;

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

    /* win32 message loop */ {
        MSG msg;
        while (user32.GetMessageW(&msg, HWND{0}, 0, 0)) {
            user32.TranslateMessage(&msg);
            user32.DispatchMessageW(&msg);
        }
    }
    return error;
}

static uint64_t now_micros()
{
    LARGE_INTEGER pc;
    global_kernel32.QueryPerformanceCounter(&pc);
    uint64_t y = pc.QuadPart - global_qpc_origin;
    y *= 1'000'000;
    y /= global_qpf_hz;
    return y;
}

static void d2d1_render(HWND hwnd);

static WIN32_WINDOW_PROC(main_window_proc)
{
    auto &main = global_uu_focus_main;
    auto const& user32 = global_user32;
    main.input.command = {};
    main.input.time_micros = now_micros();

    if (main.timer_effect) {
        main.timer_effect->now_micros = now_micros();
    }

    switch (uMsg) {
        case WM_CREATE: {
            // init
            user32.SetTimer(hWnd, /*nIDEvent*/1, 1'000, NULL);

            auto &main_state = global_uu_focus_main;
            main_state.timer_effect = timer_make(&global_platform);
            main_state.input.command = {};
            main_state.input.time_micros = now_micros();

            uu_focus_main(&main_state);
        } break;

        case WM_DESTROY: {
            main.input.command.type = Command_application_stop;
            uu_focus_main(&main);
            user32.PostQuitMessage(0);
        } break;

        case WM_LBUTTONDOWN: {
            main.input.command.type = Command_timer_start;
            uu_focus_main(&main);
        } break;

        case WM_PAINT: {
            d2d1_render(hWnd);
        } break;

        case WM_TIMER: {
            uu_focus_main(&main);
        } break;
    }
    return user32.DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

static void d2d1_render(HWND hwnd)
{
    static int count = 0;
    ++count; // paint once

    static ID2D1HwndRenderTarget* global_render_target;
    static HWND global_hwnd;
    static int global_client_width;
    static int global_client_height;

    auto& user32 = global_user32;
    auto& d2d1factory = *global_d2d1factory;

    RECT rc;
    user32.GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    HRESULT hr = S_OK;

    if (!global_render_target ||
        hwnd != global_hwnd ||
        !(width == global_client_width && height == global_client_height)) {

        hr = d2d1factory.CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(width, height)),
            &global_render_target);
        global_hwnd = hwnd;
        global_client_width = width;
        global_client_height = height;
    }

    if (S_OK != hr) return;
    auto &rt = *global_render_target;
    rt.BeginDraw();

    auto &main = global_uu_focus_main;
    if (timer_is_active(main.timer_effect)) {
        rt.Clear(D2D1::ColorF(D2D1::ColorF::Green, 1.0f));
    } else {
        rt.Clear(D2D1::ColorF(D2D1::ColorF::Black, 1.0f));
    }
    hr = rt.EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        rt.Release();
        global_render_target = nullptr;
    }
}

void platform_render(Platform* _platform)
{
    auto& platform = *_platform;
    auto& user32 = global_user32;
    auto hwnd = platform.main_hwnd;
    user32.InvalidateRect(hwnd, nullptr, FALSE);
    user32.UpdateWindow(hwnd);
}


#include "uu_focus_main.cpp"
#include "uu_focus_effects.cpp"

#include "win32_user32.cpp"
#include "win32_kernel32.cpp"