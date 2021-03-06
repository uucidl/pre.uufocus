#pragma once

template <typename FnP>
void
win32_proc_assign(
    FnP *dest, HMODULE hModule, char const* lpProcName)
{
    auto const& kernel32 = modules_kernel32;
    *dest = reinterpret_cast<FnP>(
        kernel32.GetProcAddress(hModule, lpProcName));
}




