#pragma once

#define THREAD_PROC(name_expr) DWORD WINAPI name_expr(_In_ LPVOID lpParameter)

struct kernel32
{
    HANDLE (WINAPI *CreateThread)(
        _In_opt_  LPSECURITY_ATTRIBUTES  lpThreadAttributes,
        _In_      SIZE_T                 dwStackSize,
        _In_      LPTHREAD_START_ROUTINE lpStartAddress,
        _In_opt_  LPVOID                 lpParameter,
        _In_      DWORD                  dwCreationFlags,
        _Out_opt_ LPDWORD                lpThreadId);

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