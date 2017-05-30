#pragma once

struct UITextValue
{
    char const* utf8_data_first;
    int32_t utf8_data_size;
    enum : char { MemoryOwnership_Borrowed } ownership;
};
static_assert(sizeof (UITextValue) <= sizeof (UIText), "value must fit");

