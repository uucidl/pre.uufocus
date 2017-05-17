#pragma once

struct kernel32
{
    FARPROC
        (WINAPI *GetProcAddress)(
        _In_ HMODULE hModule,
        _In_ LPCSTR  lpProcName);

    DWORD (WINAPI *GetLastError)(void);

    HMODULE (WINAPI *LoadLibraryA)(_In_ LPCSTR lpFileName);

    BOOL (WINAPI *QueryPerformanceCounter)(
        _Out_ LARGE_INTEGER *lpPerformanceCount);

    BOOL (WINAPI *QueryPerformanceFrequency)(
        _Out_ LARGE_INTEGER *lpFrequency);

};

kernel32 LoadKernel32();