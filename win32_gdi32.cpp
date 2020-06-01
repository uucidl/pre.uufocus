#include "win32_gdi32.hpp"

#include "win32_utils.ipp"

gdi32 LoadGdi32(kernel32 const& kernel32)
{
    gdi32 result;
    auto m = kernel32.LoadLibraryA("gdi32.dll");
    if (m) {
#define E(name_expr) win32_proc_assign(&result.name_expr, m, #name_expr)
        E(CreateSolidBrush);
#undef E
    }
    return result;
}


