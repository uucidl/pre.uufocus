// Tool to create a Microsoft Windows .ico file from a bunch of images.
// @language: c++11
auto const USAGE_PATTERN = "USAGE: %s [--help] --output <filepath> [--] <inputfile>+\n";

#include "3rdparty/stb_image.h"
#include "3rdparty/stb_image_write.h"

#include <string.h>

static int usage_error(char const* error_pattern, ...);
static int usage();
static int error(char const* error_pattern, ...);

struct Win32Icon;
static Win32Icon* win32icon_new();
static void win32icon_write_and_destroy(Win32Icon*, char const* filename);
static void win32icon_add_image8(Win32Icon*, stbi_uc* data8, int size_x, int size_y, int channel_count);

static char const * global_usage_program_name = "<unknown>";

int main(int argc, char const ** argv)
{
    global_usage_program_name = *(argv + 0);
    char const* output_filepath = NULL;
    size_t input_file_n = 0;
    char const** input_file_f;
    /* parse args */ {
        auto c = argv + 1;
        auto const l = argv + argc;
        /* consume options */ while (c != l) {
            if (0 == strcmp("--output", *c)) {
                ++c;
                if (c == l) { return usage_error("--output: needs one file.\n"); }
                output_filepath = *c;
            } else if (0 == strcmp("--help", *c)) {
                return usage();
            } else if (0 == strcmp("--", *c)) {
                ++c; break; // end of options
            } else if ((*c)[0] == '-') {
                return usage_error("%s: unknown flag\n", *c);
            } else {
                break; // this isn't an option, move on
            }
            ++c;
        }
        input_file_f = c;
        input_file_n = l - c;
    }
    if (!output_filepath) return usage_error("output: need one file.\n");
    if (input_file_n == 0) return usage_error("inputfile: need at least one.\n");

    auto output = win32icon_new();
    /* append images */ for (int i = 0; i != input_file_n; ++i) {
        auto const filename = input_file_f[i];
        int size_x, size_y;
        auto data8 = stbi_load(filename, &size_x, &size_y, NULL, 4);
        if (!data8) return error("%s: can't load image (%s)", filename, stbi_failure_reason());
        win32icon_add_image8(output, data8, size_x, size_y, 4);
    }
    win32icon_write_and_destroy(output, output_filepath);
}

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <vector>

static int usage()
{
    return usage_error(""), 0;
}

static int usage_error(char const* error_pattern, ...)
{
    va_list pattern_args;
    va_start(pattern_args, error_pattern);
    char buffer[4096];
    auto res = vsnprintf(buffer, sizeof buffer, error_pattern, pattern_args);
    assert (res >= 0);
    if (res >= 0) {
        fputs(buffer, stderr);
    }
    fprintf(stdout, USAGE_PATTERN, global_usage_program_name);
    return 1;
}

static int error(char const* error_pattern, ...)
{
    va_list pattern_args;
    va_start(pattern_args, error_pattern);
    char buffer[4096];
    auto res = vsnprintf(buffer, sizeof buffer, error_pattern, pattern_args);
    assert (res >= 0);
    if (res >= 0) {
        fprintf(stderr, "ERROR: %s", buffer);
    }
    return 2;
}

static void fatal_ifnot(bool cond, char const* message)
{
    if (!cond) {
        exit(error(message));
        exit(3);
    }
}

struct Win32Icon
{
    std::vector<uint8_t> data;
    std::vector<uint8_t> header;
    size_t firstIconDirEntryOffset;
    size_t nextIconDirEntryOffset;
};

struct MemoryBlock
{
    uint8_t* bytes_f;
    size_t bytes_n;
};

MemoryBlock allocate_region(std::vector<uint8_t>* memory_, size_t offset, size_t n)
{
    auto& memory = *memory_;
    if (offset + n > memory.capacity()) {
        memory.reserve(2*(offset + n));
    }
    if (offset + n > memory.size()) {
        memory.resize(offset + n);
    }
    return { memory.data() + offset, n };
}

template <typename T>
void
assign_allocate(std::vector<uint8_t>* memory_, T** temp_pointer_, size_t offset)
{
    auto& memory = *memory_;
    auto& temp_pointer = *temp_pointer_;
    auto region = allocate_region(&memory, offset, sizeof *temp_pointer);
    temp_pointer = reinterpret_cast<T*>(region.bytes_f);
}

template <typename T>
void push_back(std::vector<uint8_t>* memory_, T x)
{
    auto& memory = *memory_;
    auto region = allocate_region(&memory, memory.size(), sizeof T);
    memcpy(region.bytes_f, &x, sizeof T);
}

// structures for the ICO file format:
#pragma pack(push)
struct ICO_IconDirEntry
{
    uint8_t bWidth;
    uint8_t bHeight;
    uint8_t bColorCount;
    uint8_t bReserved;
    uint16_t wPlanes;
    uint16_t wBitCount;
    uint32_t dwBytesInRes;
    uint32_t dwImageOffset;
};
#pragma pack(pop)

#pragma pack(push)
struct ICO_IconDir
{
    uint16_t idReserved;
    uint16_t idType;
    uint16_t idCount;
    /* ICO_IconDirEntry idEntries[idCount]; */
};
#pragma pack(pop)

static Win32Icon* win32icon_new()
{
    Win32Icon* icon_ = (Win32Icon*)calloc(sizeof *icon_, 1);
    auto &icon = *icon_;

    icon.data.reserve(4096);
    icon.header.reserve(64);
    // Initialize icon directory:
    size_t iconDirOffset = 0;
    ICO_IconDir* dir_;
    assign_allocate(&icon.header, &dir_, iconDirOffset);

    auto &dir = *dir_;
    dir = {};
    dir.idReserved = 0;
    dir.idType = /* icon type*/ 1;
    dir.idCount = 0;

    icon.firstIconDirEntryOffset = iconDirOffset + sizeof dir;
    icon.nextIconDirEntryOffset = icon.firstIconDirEntryOffset;
    return &icon;
}

struct Win32IconWriteContext
{
    std::vector<uint8_t>* dest;
};

static void win32icon_data_write(Win32IconWriteContext* context_, void *data, int size)
{
    auto &dest = *context_->dest;
    auto region = allocate_region(&dest, dest.size(), size);
    memcpy(region.bytes_f, data, size);
}

static void win32icon_data_fill_n(Win32IconWriteContext* context_, uint8_t byte, size_t n)
{
    auto &dest = *context_->dest;
    auto region = allocate_region(&dest, dest.size(), n);
    memset(region.bytes_f, byte, n);
}

static void win32icon_data_write_opaque(void *context_, void *data_, int size)
{
    win32icon_data_write(
        reinterpret_cast<Win32IconWriteContext*>(context_),
        data_, size);
}

struct ICO_BitmapInfoHeader
{
    uint32_t size = 40;
    uint32_t size_x;
    uint32_t size_y;
    uint16_t color_planes_n = 1;
    uint16_t bits_per_pixel;
    uint32_t compression_method;
    uint32_t image_size = 0;
};

template <typename I>
I round_up_multiple_power_of_two(I x, I power)
{
    auto mask = (1<<power) - 1;
    return (x + mask) & ~mask;
}

static void win32icon_add_image8(Win32Icon* icon_, stbi_uc* data8, int size_x, int size_y, int channel_count)
{
    fatal_ifnot (size_x <= 256, "Invalid width\n");
    fatal_ifnot (size_y <= 256, "Invalid height\n");
    fatal_ifnot (channel_count == 3 || channel_count == 4, "RGBA or RGB only\n");
    auto &icon = *icon_;
    ICO_IconDirEntry* dir_entry_;
    assign_allocate(&icon.header, &dir_entry_, icon.nextIconDirEntryOffset);
    auto &dir_entry = *dir_entry_;
    icon.nextIconDirEntryOffset += sizeof dir_entry;
    dir_entry.bWidth = size_x < 256? size_x : 0;
    dir_entry.bHeight = size_y < 256? size_y : 0;
    dir_entry.bColorCount = 0;
    dir_entry.bReserved = 0;
    dir_entry.wPlanes = 0;
    dir_entry.wBitCount = 8*channel_count;

    Win32IconWriteContext writes = {};
    writes.dest = &icon.data;
    size_t data_offset = icon.data.size();

    bool emit_bmplike_format = size_x*size_x < 32*32;
    if (emit_bmplike_format)
    {
        ICO_BitmapInfoHeader header = {};
        header.size_x = size_x;
        header.size_y = /* xor mask */ size_y + /* and mask */ size_y;
        header.bits_per_pixel = 8*3;
        win32icon_data_write(&writes, &header, sizeof header);
        win32icon_data_fill_n(&writes, 0, header.size - sizeof header);

        int row_byte_size = round_up_multiple_power_of_two(size_x * 3, 2);
        size_t const input_row_size = size_x * channel_count;
        size_t input_row_offset = (size_y - 1) * input_row_size;
        for (int row_n = size_y; row_n--; ) {
            auto d_row_region = allocate_region(&icon.data, icon.data.size(), row_byte_size);
            auto d_byte = d_row_region.bytes_f;
            auto s_byte = data8 + input_row_offset;
            for (int x = 0; x < header.size_x; ++x) {
                /* bgr <- rgb */ for (auto comp = 3; comp--; ) {
                    *d_byte = s_byte[comp];
                    ++d_byte;
                }
                s_byte += channel_count;
            }
            memset(d_byte, 0, d_row_region.bytes_f + d_row_region.bytes_n - d_byte);
            input_row_offset -= input_row_size;
        }
        /* and mask */ {
            int row_byte_size = round_up_multiple_power_of_two(size_x/8, 2);
            auto region = allocate_region(
                &icon.data, icon.data.size(),
                size_y * row_byte_size);
            memset(region.bytes_f, 0x00, region.bytes_n);
        }
    } else {
        stbi_write_png_to_func(
            win32icon_data_write_opaque,
            &writes,
            size_x, size_y, 4, data8, 0);
    }
    dir_entry.dwBytesInRes = icon.data.size() - data_offset;
    dir_entry.dwImageOffset = data_offset;
}

static void win32icon_write_and_destroy(Win32Icon* icon_, char const* filename)
{
    Win32Icon& icon = *icon_;

    /* fixup offsets */ {
        fatal_ifnot(icon.nextIconDirEntryOffset == icon.header.size(), "defect");

        ICO_IconDir* icondir_;
        assign_allocate(&icon.header, &icondir_, 0);
        ICO_IconDir& icondir = *icondir_;
        icondir.idCount = (icon.nextIconDirEntryOffset - icon.firstIconDirEntryOffset) / sizeof (ICO_IconDirEntry);
        ICO_IconDirEntry* entry_c;
        assign_allocate(&icon.header, &entry_c, icon.firstIconDirEntryOffset);
        ICO_IconDirEntry* entry_l;
        assign_allocate(&icon.header, &entry_l, icon.nextIconDirEntryOffset);
        for (; entry_c != entry_l; ++entry_c) {
            (*entry_c).dwImageOffset += icon.header.size();
        }
    }

    auto file = fopen(filename, "wb");
    fatal_ifnot(file, "could not open output filename");
    for (auto const block_ : { &icon.header, &icon.data }) {
        auto const& block = *block_;
        auto wrote = fwrite(block.data(), 1, block.size(), file);
        fatal_ifnot(wrote == block.size(), "could not write entire file");
    }
    fclose(file);
    icon = {};
}

#define STB_IMAGE_IMPLEMENTATION
#include "3rdparty/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "3rdparty/stb_image_write.h"