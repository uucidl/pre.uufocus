// @os: win32

#define WIN32_CALLBACK __stdcall

namespace win32
{
    typedef void *HINSTANCE;
}
 
int WIN32_CALLBACK WinMain(
win32::HINSTANCE hI,
win32::HINSTANCE hPI,
char const* lpCmdLine,
int nCmdShow)
{
    return 0;
}
