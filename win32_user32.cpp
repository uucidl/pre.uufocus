#include "win32_user32.hpp"

#include "win32_utils.ipp"

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
        E(KillTimer);
        E(LoadImageW);
        E(MessageBoxW);
        E(PostQuitMessage);
        E(RegisterClassExW);
        E(SetTimer);
        E(ShowWindow);
        E(TranslateMessage);
        E(UpdateWindow);
#undef E
    }
    return result;
}
