// @os: win32

#include "uu_focus_main.hpp"

#include <sal.h>
#include <stdint.h>
#include <windows.h>

#include "win32_kernel32.hpp"
#include "win32_user32.hpp"


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

    /* d2d1 */ {
        auto m = kernel32.LoadLibraryA("d2d1.dll");
        if (!m) {
            return 1; // failed to load d2d1
        }
    }

    // init
    global_uu_focus_main.input.command = {};
    global_uu_focus_main.input.time_micros = now_micros();
    uu_focus_main(&global_uu_focus_main);

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

static WIN32_WINDOW_PROC(main_window_proc)
{
    auto &main = global_uu_focus_main;
    auto const& user32 = global_user32;
    main.input.command = {};
    main.input.time_micros = now_micros();

    switch (uMsg) {
        case WM_LBUTTONDOWN: {
            main.input.command.type = Command_timer_start;
        } break;

        case WM_DESTROY: {
            main.input.command.type = Command_application_stop;
        } break;
    }

    bool must_quit = main.input.command.type == Command_application_stop;
    uu_focus_main(&main);
    if (must_quit) {
        user32.PostQuitMessage(0);
    }
    return user32.DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

#include "uu_focus_main.cpp"
#include "uu_focus_effects.cpp"

#include "win32_user32.cpp"
#include "win32_kernel32.cpp"