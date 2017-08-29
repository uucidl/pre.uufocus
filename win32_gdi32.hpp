#pragma once

struct gdi32
{
    HBRUSH (WINAPI *CreateSolidBrush)(
        _In_ COLORREF crColor);
};

gdi32 LoadGdi32(kernel32 const& kernel32);


