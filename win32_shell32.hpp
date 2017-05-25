#pragma once

struct shell32
{
    BOOL (*Shell_NotifyIconW)(
        _In_ DWORD           dwMessage,
        _In_ PNOTIFYICONDATA lpdata);
};

shell32 LoadShell32(kernel32 const& kernel32);


