#pragma once
#define WIN32_USER32

#define WIN32_WINDOW_PROC(expr) \
LRESULT expr(                   \
    _In_ HWND   hWnd,           \
    _In_ UINT   uMsg,           \
    _In_ WPARAM wParam,         \
    _In_ LPARAM lParam)

struct user32
{
    HWND (WINAPI *CreateWindowExW)(
        DWORD     dwExStyle,
        wchar_t const*   lpClassName,
        wchar_t const*   lpWindowName,
        DWORD     dwStyle,
        int              x,
        int              y,
        int              nWidth,
        int              nHeight,
        HWND      hWndParent,
        HMENU     hMenu,
        HINSTANCE hInstance,
        void*            lpParam);

    LRESULT (WINAPI *DefWindowProcW)(
        HWND   hWnd,
        UINT   uMsg,
        WPARAM wParam,
        LPARAM lParam);

    BOOL (WINAPI *DispatchMessageW)(MSG *lpMsg);

    BOOL (WINAPI *GetClientRect)(
        _In_  HWND   hWnd,
        _Out_ LPRECT lpRect);

    BOOL (WINAPI *GetMessageW)(
        MSG *lpMsg,
        HWND  hWnd,
        UINT  wMsgFilterMin,
        UINT  wMsgFilterMax);

    BOOL (WINAPI *InvalidateRect)(
        _In_       HWND hWnd,
        _In_ const RECT *lpRect,
        _In_       BOOL bErase);

    VOID (WINAPI *PostQuitMessage)(
        _In_ int nExitCode);

    ATOM (WINAPI *RegisterClassExW)(_In_ const WNDCLASSEXW *lpwcx);

    BOOL (WINAPI *ShowWindow)(
        HWND hWnd,
        int nCmdShow);

    UINT_PTR (WINAPI *SetTimer)(
        _In_opt_ HWND      hWnd,
        _In_     UINT_PTR  nIDEvent,
        _In_     UINT      uElapse,
        _In_opt_ TIMERPROC lpTimerFunc);

    BOOL (WINAPI *TranslateMessage)(MSG *lpMsg);

    BOOL (WINAPI *UpdateWindow)(
        _In_ HWND hWnd);
};

user32 LoadUser32(kernel32 const& kernel32);
