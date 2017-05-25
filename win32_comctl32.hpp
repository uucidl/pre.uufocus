#pragma once

struct comctl32
{
    HRESULT (WINAPI *LoadIconMetric)(
        _In_  HINSTANCE hinst,
        _In_  PCWSTR    pszName,
        _In_  int       lims,
        _Out_ HICON     *phico);
};

struct kernel32;
comctl32 LoadComctl32(kernel32 const&);

