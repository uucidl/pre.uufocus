#include "win32_shell32.hpp"

#include "win32_kernel32.hpp"

#include "win32_utils.ipp"

shell32 LoadShell32(kernel32 const& kernel32)
{
    shell32 result;
    auto m = kernel32.LoadLibraryA("shell32.dll");
    if (m) {
#define E(name_expr) address_assign(&result.name_expr, m, #name_expr)
        E(Shell_NotifyIconW);
#undef E
    }
    return result;

}



