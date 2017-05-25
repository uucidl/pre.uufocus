#pragma once

struct kernel32
{
    FARPROC
        (WINAPI *GetProcAddress)(
        _In_ HMODULE hModule,
        _In_ LPCSTR  lpProcName);

    DWORD (WINAPI *GetLastError)(void);

    HMODULE (WINAPI *LoadLibraryA)(_In_ LPCSTR lpFileName);

    int (WINAPI *MultiByteToWideChar)(
        _In_      UINT   CodePage,
        _In_      DWORD  dwFlags,
        _In_      LPCSTR lpMultiByteStr,
        _In_      int    cbMultiByte,
        _Out_opt_ LPWSTR lpWideCharStr,
        _In_      int    cchWideChar);

    BOOL (WINAPI *QueryPerformanceCounter)(
        _Out_ LARGE_INTEGER *lpPerformanceCount);

    BOOL (WINAPI *QueryPerformanceFrequency)(
        _Out_ LARGE_INTEGER *lpFrequency);

};

kernel32 LoadKernel32();