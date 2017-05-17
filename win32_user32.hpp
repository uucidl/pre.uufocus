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

    BOOL (WINAPI *GetMessageW)(
        MSG *lpMsg,
        HWND  hWnd,
        UINT  wMsgFilterMin,
        UINT  wMsgFilterMax);

    VOID (WINAPI *PostQuitMessage)(
        _In_ int nExitCode);

    ATOM (WINAPI *RegisterClassExW)(_In_ const WNDCLASSEXW *lpwcx);

    BOOL (WINAPI *ShowWindow)(
        HWND hWnd,
        int nCmdShow);

    BOOL (WINAPI *TranslateMessage)(MSG *lpMsg);
};

user32 LoadUser32(kernel32 const& kernel32);
