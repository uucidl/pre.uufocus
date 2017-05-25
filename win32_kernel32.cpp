#include "win32_kernel32.hpp"

#include <windows.h>

kernel32 LoadKernel32()
{
    kernel32 result = {};
    result.GetProcAddress = ::GetProcAddress;
    result.GetLastError = ::GetLastError;
    result.LoadLibraryA = ::LoadLibraryA;
    result.MultiByteToWideChar = ::MultiByteToWideChar;
    result.QueryPerformanceCounter = ::QueryPerformanceCounter;
    result.QueryPerformanceFrequency = ::QueryPerformanceFrequency;
    return result;
}


