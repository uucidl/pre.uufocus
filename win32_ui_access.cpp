// accessibility

#if defined(WIN32_UI_ACCESS_MAIN)

#define UNICODE
#include <windows.h>
#pragma comment(lib, "user32.lib")

struct Content
{
    int min_x, min_y;
    int max_x, max_y;
    char const* text_utf8;
    int text_utf8_n;
};

struct DocumentPart
{
    int parent_index;
    int children_index_first;
    int children_index_last;
    Content content;
};

struct DocumentTree
{
    enum { MAX_PART_N = 64 };
    DocumentPart parts[MAX_PART_N];
    int parts_n;
};

#include <uiautomation.h>
#pragma comment(lib, "uiautomationcore.lib")

#include <assert.h>

// Glue between document & Uia
struct DocumentPartProvider : public IRawElementProviderSimple, public IRawElementProviderFragment, public IRawElementProviderFragmentRoot
{
    LONG reference_count;
    HWND hwnd;
    DocumentTree* document_tree_;
    int part_i;

    // IUnknown
    ULONG AddRef() override
    {
        ++reference_count;
        return reference_count;
    }

    ULONG Release() override
    {
        assert(reference_count > 0);
        --reference_count;
        if (!reference_count) delete this;
    }

    HRESULT QueryInterface(REFIID riid, VOID **ppvInterface) override
    {
        *ppvInterface = nullptr;
        if (__uuidof(IRawElementProviderSimple) == riid) {
            *ppvInterface = static_cast<IRawElementProviderSimple*>(this);
        } else if (__uuidof(IRawElementProviderFragment) == riid) {
            *ppvInterface = static_cast<IRawElementProviderFragment*>(this);
        } else if (__uuidof(IRawElementProviderFragmentRoot) == riid) {
            if (part_i == 0) {
                *ppvInterface = static_cast<IRawElementProviderFragmentRoot*>(this);
            }
        }

        if (!*ppvInterface) return E_NOINTERFACE;
        this->AddRef();
        return S_OK;
    }
};

static DocumentTree global_document;

static void demo_document_init(DocumentTree* document_tree_)
{
    DocumentTree& document = *document_tree_;
    // Add synthetic root at index 0
    ++document.parts_n;
    auto &root = document.parts[0];

    // Add container
    {
        auto part_i = document.parts_n++;
        root.children_index_first = part_i;
        root.children_index_last = part_i + 1;

        auto child1_part_i = document.parts_n++;
        auto child2_part_i = document.parts_n++;

        document.parts[part_i].children_index_first = child1_part_i;
        document.parts[part_i].children_index_last = child2_part_i + 1;

        auto& child1 = document.parts[child1_part_i].content;
        child1.text_utf8 = "Hello, world";
        child1.text_utf8_n = 7;

        auto& child2 = document.parts[child2_part_i].content;
        child2.text_utf8 = "world!";
        child2.text_utf8_n = 6;
    }
}

static void document_layout_horizontal_split(DocumentTree* document_tree_,
                                  int parts_f, int parts_l,
                                  int min_x, int min_y,
                                  int max_x, int max_y)
{
    DocumentTree& document = *document_tree_;
    double x = min_x;
    double divisor = parts_l - parts_f;
    double x_inc = (max_x - min_x) / divisor;
    for (auto parts_i = parts_f; parts_i != parts_l; ++parts_i) {
        auto part_min_x = x;
        auto part_max_x = x + x_inc;
        auto &part = document.parts[parts_i];
        part.content.min_x = static_cast<int>(part_min_x);
        part.content.max_x = static_cast<int>(part_max_x);
        part.content.min_y = min_y;
        part.content.max_y = max_y;
        document_layout_horizontal_split(&document,
                                         part.children_index_first,
                                         part.children_index_last,
                                         part.content.min_x,
                                         part.content.min_y,
                                         part.content.max_x,
                                         part.content.max_y);
        x += x_inc;
    }
}

static void demo_document_layout(DocumentTree* document_tree_, int width, int height)
{
    DocumentTree& document = *document_tree_;

    auto min_x = 0;
    auto min_y = 0;
    auto max_x = width;
    auto max_y = height;

    document_layout_horizontal_split(&document,
                                     0, 1,
                                     min_x, min_y, max_x, max_y);
}

static LRESULT main_window_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_CREATE: {
        } break;

        case WM_DESTROY: {
            PostQuitMessage(0);
        } break;

        case WM_SIZE: {
            int width = (lParam>>0) & 0xffff;
            int height = (lParam>>16) & 0xffff;
            demo_document_layout(&global_document, width, height);
        } break;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}


extern "C" int WINAPI WinMain(
_In_ HINSTANCE hI,
_In_opt_ HINSTANCE hPI,
_In_ char* lpCmdLine,
_In_ int nCmdShow)
{
    demo_document_init(&global_document);
    auto const application_name = L"demo_win32_ui_access";
    WNDCLASSEXW main_class = {};
    {
        auto& d = main_class;
        d.cbSize = sizeof(d);
        d.style = CS_VREDRAW | CS_HREDRAW;
        d.lpfnWndProc = main_window_proc;
        d.lpszClassName = L"uu_focus";
        d.hIcon = (HICON)LoadImageW(
            GetModuleHandle(nullptr),
            IDI_APPLICATION,
            IMAGE_ICON,
            0, 0, LR_DEFAULTSIZE);
    }

    if (RegisterClassExW(&main_class) != 0) {
        DWORD window_style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        RECT window_rect = {};
        {
            window_rect.right = 600;
            window_rect.bottom = 200;
            AdjustWindowRect(&window_rect, window_style, /*bMenu*/false);
        }

        HWND main_hwnd = CreateWindowExW(
            DWORD{0},
            main_class.lpszClassName,
            application_name,
            window_style,
            /* x */ int(CW_USEDEFAULT),
            /* y */ CW_USEDEFAULT,
            /* nWidth */ window_rect.right - window_rect.left,
            /* nHeight */ window_rect.bottom - window_rect.top,
            /* hWwndParent */ HWND{0},
            HMENU{0},
            HINSTANCE{0},
            0);
        if (main_hwnd) {
            // win32 message loop
            MSG msg;
            while (GetMessageW(&msg, HWND{0}, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

    return 1;
}

#endif

