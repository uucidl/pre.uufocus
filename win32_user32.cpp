#include "win32_user32.hpp"

template <typename FnP>
void
address_assign(
    FnP *dest, HMODULE hModule, char const* lpProcName)
{
    auto const& kernel32 = global_kernel32;
    *dest = reinterpret_cast<FnP>(
        kernel32.GetProcAddress(hModule, lpProcName));
}

user32 LoadUser32(kernel32 const& kernel32)
{
    user32 result;
    auto m = kernel32.LoadLibraryA("user32.dll");
    if (m) {
#define E(name_expr) address_assign(&result.name_expr, m, #name_expr)
        E(CreateWindowExW);
        E(DefWindowProcW);
        E(DispatchMessageW);
        E(GetClientRect);
        E(GetMessageW);
        E(InvalidateRect);
        E(PostQuitMessage);
        E(RegisterClassExW);
        E(ShowWindow);
        E(TranslateMessage);
        E(UpdateWindow);
#undef E
    }
    return result;
}
