#include "uu_focus_platform.hpp"

#include "uu_focus_platform_types.hpp"

// TODO(nicolas): works only with literals for now
UIText ui_text(char const* zstr)
{
    UITextValue text;
    text.ownership = UITextValue::MemoryOwnership_Borrowed;
    text.utf8_data_first = zstr;
    int n = 0;
    while(*zstr++) ++n;
    text.utf8_data_size = n;

    UIText result;
    memcpy(&result, &text, sizeof text);
    return result;
}

