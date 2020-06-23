#include "uu_focus_platform.hpp"

#include "uu_focus_platform_types.hpp"

#include <stdarg.h>

char temp_arena_bytes[4096];
static size_t temp_arena_capacity = sizeof temp_arena_bytes;
static size_t temp_arena_size;

char* temp_allocator_zalloc(size_t size) {
  if (size >= temp_arena_capacity - temp_arena_size) {
    return 0;
  }
  char *ptr = &temp_arena_bytes[temp_arena_size];
  temp_arena_size += size;
  return ptr;
}

void temp_allocator_reset() {
  temp_arena_size = 0;
}


UIText ui_text_temp(char const* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  const auto result = ui_text_tempv(fmt, args);
  va_end(args);
  return result;
}

UIText ui_text_tempv(char const* fmt, va_list input_args)
{
    va_list args;
    va_copy(args, input_args);
    size_t needed_bytes = ::vsnprintf(nullptr, 0, fmt, args);
    va_end(args);
    if (needed_bytes >= INT_MAX) {
      return {};
    }

    size_t buffer_size = needed_bytes + 1;
    auto buffer = temp_allocator_zalloc(buffer_size);
    va_copy(args, input_args);
    ::vsnprintf(buffer, buffer_size, fmt, args);
    va_end(args);

    UITextValue text;
    text.ownership = UITextValue::MemoryOwnership_Temp;
    text.utf8_data_first = &buffer[0];
    text.utf8_data_size = int32_t(needed_bytes);

    UIText result;
    memcpy(&result, &text, sizeof text);
    return result;
}
