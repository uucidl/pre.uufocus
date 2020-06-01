#include "win32_comctl32.hpp"

#include "win32_kernel32.hpp"

#include "win32_utils.ipp"

comctl32 LoadComctl32(kernel32 const& kernel32)
{
    comctl32 result;
    auto m = kernel32.LoadLibraryA("Comctl32.dll");
    if (m) {
#define E(name_expr) win32_proc_assign(&result.name_expr, m, #name_expr)
        E(LoadIconMetric);
#undef E
    }
    return result;

}




