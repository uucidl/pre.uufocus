// # Reloadable Modules
//
// Loads dynamic libraries, and can reload them whenever they change

#ifndef uu_internal_symbol
#define uu_internal_symbol static
#endif

// marks all exported symbols
#ifndef win32_reloadable_modules_api
#define win32_reloadable_modules_api uu_internal_symbol
#endif

#include <cstdint>

// Module(win32_reloadable_modules)
// Version(1.0.2)
namespace win32_reloadable_modules
{

// Interface:

struct ReloadableModule;

// Models an immutable view on a string as a bounded range [first, last)
struct StringSpan {
        char const *first;
        char const *last;
        // Implicit conversion from zero terminated string
        StringSpan(char const *zstring) : first(zstring)
        {
                last = first;
                while (*last)
                        ++last;
        }
        StringSpan(char const *first, char const *last)
            : first(first), last(last)
        {
        }
};

// Creates a reloadable module.
//
// The module is expected as a DLL:
// - filename [module_name, module_name_end)
// - directory whose path is defined by [base_path, base_path_end)
win32_reloadable_modules_api void make_in_path(ReloadableModule *module,
                                               StringSpan base_path,
                                               StringSpan module_name);

// Returns true whenever the module is found to have
// changed
win32_reloadable_modules_api bool has_changed(ReloadableModule const *module);

enum class ReloadAttemptResult {
        NoChange,
        Unloaded,
        IsLocked,
        Success,
};

struct ReloadAttemptResultDetails {
        char const *msg;
};

// Loads and reloads a module.
//
// Precondition(has_changed(module))
//
// Build system can prevent loading of an incompletely written dll by creating a
// file named `%s_dll.lock` in the module's directory.
//
// Returns ReloadAttemptResult::IsLocked if the DLL is locked externally
// Returns ReloadAttemptResult::Success on success
//
// In case of failure, details are filled in the optional `error_details`
// argument
win32_reloadable_modules_api ReloadAttemptResult
load(ReloadableModule *module,
     ReloadAttemptResultDetails *error_details = nullptr);

struct ReloadableModule {
        void *dll;
        uint32_t modification_time_low;
        uint32_t modification_time_high;
        char *dll_path;
        char *loaded_dll_path;
        char *dll_lock_path;
        char buffer[4096];
};

// Implementation:

// ImportModule(Winapi)
namespace Winapi
{
#ifdef _WINDOWS_
// user has imported windows.h, so we simply alias

using FileAttributeData = ::WIN32_FILE_ATTRIBUTE_DATA;
using FileTime = ::FILETIME;
using GetFileExInfoLevels = ::GET_FILEEX_INFO_LEVELS;
using ModulePtr = ::HMODULE;
uu_internal_symbol auto const CompareFileTime = ::CompareFileTime;
uu_internal_symbol auto const CopyFileA = ::CopyFileA;
uu_internal_symbol auto const FreeLibrary = ::FreeLibrary;
uu_internal_symbol auto const GetFileAttributesExA = ::GetFileAttributesExA;
uu_internal_symbol auto const LoadLibraryA = ::LoadLibraryA;
uu_internal_symbol auto const MoveFileA = ::MoveFileA;
uu_internal_symbol auto const DeleteFileA = ::DeleteFileA;
#else
// user did not import windows.h, so we define it ourselves

using FileTime = struct {
        uint32_t dwLowDateTime;
        uint32_t dwHighDateTime;
};
using ModulePtr = void *;
using FileAttributeData = struct {
        uint32_t dwFileAttributes;
        FileTime ftCreationTime;
        FileTime ftLastAccessTime;
        FileTime ftLastWriteTime;
        uint32_t nFileSizeHigh;
        uint32_t nFileSizeLow;
};
enum GetFileExInfoLevels {
        GetFileExInfoStandard,
        GetFileExMaxInfoLevel,
};
using Bool = int;
#define WINAPI_api __stdcall
extern "C" int32_t WINAPI_api CompareFileTime(_In_ const FileTime *lpFileTime1,
                                              _In_ const FileTime *lpFileTime2);
extern "C" Bool WINAPI_api CopyFileA(_In_ char const *lpExistingFileName,
                                     _In_ char const *lpNewFileName,
                                     _In_ Bool bFailIfExists);

extern "C" Bool WINAPI_api DeleteFile(_In_ char const *lpFileName);

extern "C" Bool WINAPI_api FreeLibrary(_In_ ModulePtr hModule);

extern "C" Bool WINAPI_api
GetFileAttributesExA(_In_ char const *lpFileName,
                     _In_ GetFileExInfoLevels fInfoLevelId,
                     _Out_ void *lpFileInformation);

extern "C" ModulePtr WINAPI_api LoadLibraryA(_In_ const char *lpFileName);

extern "C" Bool WINAPI_api MoveFileA(_In_ char const *lpExistingFileName,
                                     _In_ char const *lpNewFileName);

#undef WINAPI_api
#endif
} // WinApi namespace

uu_internal_symbol char *
copy_bounded_nonoverlapping(char const *sf, char const *sl, char *df, char *dl)
{
        while (sf != sl && df != dl) {
                *df = *sf;
                ++sf;
                ++df;
        }
        return df;
}

uu_internal_symbol void fill_zero(uint8_t *f, uint64_t count)
{
        while (count--)
                *f = 0;
}

uu_internal_symbol inline Winapi::FileTime win32_get_last_write_time(char *path)
{
        using namespace Winapi;
        FileTime result = {};
        FileAttributeData attributes;
        if (Winapi::GetFileAttributesExA(path, GetFileExInfoStandard,
                                         &attributes)) {
                result = attributes.ftLastWriteTime;
        }
        return result;
}

bool has_changed(ReloadableModule const *module)
{
        Winapi::FileTime previous_mod_time;
        previous_mod_time.dwLowDateTime = module->modification_time_low;
        previous_mod_time.dwHighDateTime = module->modification_time_high;
        auto dll_modification_time =
            win32_get_last_write_time(module->dll_path);
        return 0 != Winapi::CompareFileTime(&dll_modification_time,
                                            &previous_mod_time);
}

ReloadAttemptResult load(ReloadableModule *module,
                         ReloadAttemptResultDetails *error_details_output)
{
        char *source_dll_path = module->dll_path;
        char *loaded_dll_path = module->loaded_dll_path;
        char *lock_file_path = module->dll_lock_path;
        const char *error = nullptr;

        ReloadAttemptResult result = ReloadAttemptResult::NoChange;
        Winapi::FileAttributeData ignored;
        if (!Winapi::GetFileAttributesExA(
                lock_file_path,
                Winapi::GetFileExInfoLevels::GetFileExInfoStandard, &ignored)) {
                auto file_time = win32_get_last_write_time(source_dll_path);
                {
                        auto old_dll = module->dll;
                        if (old_dll) {
                                if (!Winapi::FreeLibrary(
                                        Winapi::ModulePtr(old_dll))) {
                                        error = "could not unload old version";
                                } else {
                                        result = ReloadAttemptResult::Unloaded;
                                }
                        } else {
                                result = ReloadAttemptResult::Unloaded;
                        }
                        if (result == ReloadAttemptResult::Unloaded) {
                                if (!Winapi::CopyFileA(source_dll_path,
                                                       loaded_dll_path,
                                                       false)) {
                                        error = "could not copy dll";
                                } else {
                                        auto new_dll = Winapi::LoadLibraryA(
                                            loaded_dll_path);
                                        if (new_dll) {
                                                result = ReloadAttemptResult::
                                                    Success;
                                                module->dll = new_dll;
                                                module->modification_time_low =
                                                    file_time.dwLowDateTime;
                                                module->modification_time_high =
                                                    file_time.dwHighDateTime;
                                        }
                                }
                        }
                }
        } else {
                // NOTE(nicolas): the lock file exists, so the compiler is busy
                // producing the dll. We do not load it then. Try again later.
                result = ReloadAttemptResult::IsLocked;
        }
        if (error_details_output) {
                *error_details_output = {error};
        }
        return result;
}

void make_in_path(ReloadableModule *module,
                  StringSpan base_path,
                  StringSpan module_name)
{
        fill_zero(reinterpret_cast<uint8_t *>(module), sizeof *module);
        // arena
        auto buffer = module->buffer;
        auto const buffer_last = buffer + sizeof(module->buffer);

        struct {
                char **output_zstring;
                StringSpan suffix;
        } paths[] = {
            {
                &module->dll_path, ".dll",
            },
            {
                &module->loaded_dll_path, "_loaded.dll",
            },
            {
                &module->dll_lock_path, "_dll.lock",
            },
        };

        for (auto const path : paths) {
                auto first = buffer;
                auto current = first;
                auto const last = buffer_last;
                current = copy_bounded_nonoverlapping(
                    base_path.first, base_path.last, current, last);
            if (current != last && *current != '\\') {
                *current = '\\';
                ++current;
            }
                current = copy_bounded_nonoverlapping(
                    module_name.first, module_name.last, current, last);
                current = copy_bounded_nonoverlapping(
                    path.suffix.first, path.suffix.last, current, last);
                if (current != last) {
                        *current++ = '\0';
                }
                *path.output_zstring = first;
                buffer = current;
        }
        // TODO(nicolas): detect when we exhaust the buffer
}

} // win32_ReloadableModules namespace
