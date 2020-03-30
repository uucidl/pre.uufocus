#include "win32_kernel32.hpp"

#include <windows.h>

kernel32 LoadKernel32()
{
    kernel32 result = {};
    result.CreateEventW = ::CreateEventW;
    result.CreateThread = ::CreateThread;
    result.GetProcAddress = ::GetProcAddress;
    result.GetLastError = ::GetLastError;
    result.LoadLibraryA = ::LoadLibraryA;
    result.MultiByteToWideChar = ::MultiByteToWideChar;
    result.QueryPerformanceCounter = ::QueryPerformanceCounter;
    result.QueryPerformanceFrequency = ::QueryPerformanceFrequency;
    result.SetEvent = ::SetEvent;
    result.GetLocalTime = ::GetLocalTime;
    return result;
}


