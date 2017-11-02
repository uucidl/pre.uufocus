// accessibility

// TODO(nil): make text discoverable by the windows narrator
//
// Example of text from a web page that the narrator can deal with
//
// ControlType: UIA_EditControlTypeId
// IsEnabled: true
// IsValuePatternAvailable: true

#if defined(WIN32_UI_ACCESS_MAIN)

// The document tree is meant to represent the UI's structure to assist
// fast navigation, offer access to its content as well as its controls.

#define UNICODE
#include <windows.h>
#pragma comment(lib, "user32.lib")

#include <cfloat>
#include <cstdint>

enum ReadingLayout
{
    ReadingLayout_LeftToRight,
    ReadingLayout_RightToLeft,
    ReadingLayout_TopDown,
};

struct Float32_2
{
    float x, y;
};

struct Float32Box2
{
    Float32_2 min, max;
};

Float32Box2 box2_min_max_identity()
{
    Float32Box2 y;
    y.min.x = y.min.y = FLT_MAX;
    y.max.x = y.max.y = -FLT_MAX;
    return y;
}

struct Content
{
    Float32Box2 box;
    char const* text_utf8;
    int text_utf8_n;
    ReadingLayout orientation;
};

struct DocumentPart
{
    int parent_index;
    int children_index_first;
    int children_index_last;
    Content content;
    // stable id representing the part regardless of its localized content
    uint64_t part_id;
};

struct DocumentTree
{
    Float32Box2 window_with_decoration;
    enum { MAX_PART_N = 64 };
    DocumentPart parts[MAX_PART_N];
    int parts_n;
    int active_part_i;
};

#include <uiautomation.h>
#pragma comment(lib, "uiautomationcore.lib")

#include <windows.h>
#pragma comment(lib, "user32.lib")

#include <OleAuto.h>
#pragma comment(lib, "OleAut32.lib")

#include <assert.h>

static Float32Box2 physical_screen_from_logical_window(HWND hwnd,
                                                       Float32Box2 box)
{
    POINT points[] = {
        { (LONG)box.min.x, (LONG)box.min.y },
        { (LONG)box.max.x, (LONG)box.max.y }
    };
    // TODO(uucidl): @sloppy error handling
    MapWindowPoints(hwnd, HWND_DESKTOP, points, /* point count */ 2);
    LogicalToPhysicalPoint(hwnd, &points[0]);
    LogicalToPhysicalPoint(hwnd, &points[1]);
    return {
        { (float)points[0].x, (float)points[0].y },
        { (float)points[1].x, (float)points[1].y }
    };
}

static Float32_2 logical_point_from_physical_screen(HWND hwnd, Float32_2 point)
{
    POINT p = { (LONG) point.x, (LONG) point.y };
    MapWindowPoints(HWND_DESKTOP, hwnd, &p, 1);
    return {
        (float) p.x,
        (float) p.y
    };
}

static void to_VT_R8_ARRAY(Float32Box2 box, VARIANT* d_)
{
    auto &d = *d_;
    // TODO(uucidl): @sloppy error handling
    auto sa = SafeArrayCreateVector(VT_R8, 0, 4);
    LONG d_i;
    d_i = 0;
    SafeArrayPutElement(sa, &d_i, (void*)&box.min.x); ++d_i;
    SafeArrayPutElement(sa, &d_i, (void*)&box.min.y); ++d_i;
    SafeArrayPutElement(sa, &d_i, (void*)&box.max.x); ++d_i;
    SafeArrayPutElement(sa, &d_i, (void*)&box.max.y); ++d_i;
    d.vt = VT_R8 | VT_ARRAY;
    d.parray = sa;
}

static void to_BSTR_from_small(char const* utf8, int utf8_n, VARIANT* d_)
{
    auto &d = *d_;
    // TODO(uucidl): @sloppy error handling
    enum { MAX_TEXT_SIZE = 100 };
    wchar_t result_text[MAX_TEXT_SIZE];
    auto const result_text_n =
        MultiByteToWideChar(
        CP_UTF8,
        0,
        utf8,
        utf8_n,
        result_text,
        MAX_TEXT_SIZE);
    auto bstr = SysAllocStringLen(result_text, result_text_n);
    d.vt = VT_BSTR;
    d.bstrVal = bstr;
}

static void to_BSTR_hex_from_uint64(uint64_t x, VARIANT* d_)
{
    static constexpr auto MAX_HEX_DIGITS = 64/4;
    wchar_t hex[MAX_HEX_DIGITS + 1];
    auto hex_l = hex;
    for (int nibble_i = MAX_HEX_DIGITS; nibble_i; --nibble_i) {
        const auto nibble = (x >> 4*(nibble_i - 1)) & 0xF;
        if (nibble < 10) {
            *hex_l = L'0' + char(nibble);
        } else {
            *hex_l = L'a' + char(nibble - 10);
        }
        ++hex_l;
    }
    auto bstr = SysAllocStringLen(hex, (UINT)(hex_l - hex));

    auto &d = *d_;
    d.vt = VT_BSTR;
    d.bstrVal = bstr;
}


// Glue between document & Uia
struct DocumentPartProvider
: public IRawElementProviderSimple
, public IRawElementProviderFragment
, public IRawElementProviderFragmentRoot
{
    LONG reference_count = 0;
    HWND hwnd;
    DocumentTree* document_tree_;
    int part_i;

    // IUnknown:
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
        return reference_count;
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

    // IRawElementProviderSimple:
    HRESULT get_ProviderOptions(ProviderOptions *pRetVal) override
    {
        *pRetVal = ProviderOptions_ServerSideProvider
            | ProviderOptions_UseComThreading;
        return S_OK;
    }

    HRESULT get_HostRawElementProvider(IRawElementProviderSimple **pRetVal) override
    {
        if (this->part_i == 0) return UiaHostProviderFromHwnd(this->hwnd, pRetVal);
        *pRetVal = nullptr;
        return S_OK;
    }

    HRESULT GetPatternProvider(
        PATTERNID patternId, IUnknown  **pRetVal) override
    {
        *pRetVal = nullptr;
        return S_OK;
    }

    HRESULT GetPropertyValue(PROPERTYID propertyId, VARIANT *pRetVal) override
    {
        auto &result = *pRetVal;
        result.vt = VT_EMPTY;

        auto const& document = *this->document_tree_;
        auto const& part_i = this->part_i;
        auto const& content = document.parts[part_i].content;
        switch (propertyId)
        {
            case UIA_AutomationIdPropertyId: {
                if (document.parts[part_i].part_id) {
                    to_BSTR_hex_from_uint64(document.parts[part_i].part_id, &result);
                }
            } break;

            case UIA_BoundingRectanglePropertyId: {
                if (part_i != 0) {
                    auto const box = physical_screen_from_logical_window(
                        hwnd,
                        content.box);
                    to_VT_R8_ARRAY(box, &result);
                }
            } break;

            case UIA_HasKeyboardFocusPropertyId: {
                if (part_i != 0) {
                    result.vt = VT_BOOL;
                    result.boolVal = document.active_part_i == part_i;
                }
            } break;

            case UIA_IsKeyboardFocusablePropertyId: {
                if (part_i != 0) {
                    result.vt = VT_BOOL;
                    result.boolVal = VARIANT_TRUE;
                }
            } break;

            case UIA_IsEnabledPropertyId: {
                result.vt = VT_BOOL;
                result.boolVal = VARIANT_TRUE;
            } break;

            case UIA_NamePropertyId: {
                if (part_i != 0) {
                    to_BSTR_from_small(
                        content.text_utf8, content.text_utf8_n, &result);
                }
            } break;
        }
        return S_OK;
    }

    // IRawElementProviderFragment:
    HRESULT GetEmbeddedFragmentRoots(SAFEARRAY **ppArray) override
    {
        *ppArray = nullptr;
        return S_OK;
    }

    HRESULT GetRuntimeId(SAFEARRAY **ppArray) override
    {
        auto const& part_i = this->part_i;
        if (part_i == 0) *ppArray = nullptr;
        else {
            int rId[] = { UiaAppendRuntimeId, -1 };
            int rId_n = 2;
            SAFEARRAY* sa = SafeArrayCreateVector(VT_I4, 0, rId_n);
            LONG d_i = 0;
            SafeArrayPutElement(sa, &d_i, (void*)&rId[0]);
            ++d_i;
            SafeArrayPutElement(sa, &d_i, (void*)&rId[1]);
            ++d_i;
            *ppArray = sa;
        }
        return S_OK;
    }

    HRESULT Navigate(NavigateDirection direction, IRawElementProviderFragment **pRetVal) override
    {
        *pRetVal = nullptr;

        auto const& document = *this->document_tree_;
        auto const& part_i = this->part_i;
        auto const& part = document.parts[part_i];

        int other_part_i = DocumentTree::MAX_PART_N;
        switch (direction)
        {
            case NavigateDirection_NextSibling:
            case NavigateDirection_PreviousSibling:
            case NavigateDirection_Parent:
            {
                if (part_i > 0) {
                    int parent_part_i = 0;
                    int siblings_f = 0;
                    int siblings_l = 0;
                    for (auto const part : document.parts) {
                        siblings_f = part.children_index_first;
                        siblings_l = part.children_index_last;
                        if (part_i >=  siblings_f &&
                            part_i < siblings_l)
                        {
                            break;
                        }
                        ++parent_part_i;
                    }
                    assert(parent_part_i < DocumentTree::MAX_PART_N);

                    auto const& parent_part = document.parts[parent_part_i];
                    if (NavigateDirection_Parent == direction) {
                        other_part_i = parent_part_i;
                    } else if (NavigateDirection_NextSibling == direction) {
                        if (part_i + 1 < siblings_l) {
                            other_part_i = part_i + 1;
                        }
                    } else if (NavigateDirection_PreviousSibling == direction) {
                        if (part_i > siblings_f) {
                            other_part_i = part_i - 1;
                        }
                    }
                }
            } break;

            case NavigateDirection_FirstChild: {
                if (part.children_index_first != part.children_index_last) {
                    other_part_i = part.children_index_first;
                }
            } break;

            case NavigateDirection_LastChild: {
                if (part.children_index_first != part.children_index_last) {
                    other_part_i = part.children_index_last - 1;
                }
            } break;
        }

        if (other_part_i != DocumentTree::MAX_PART_N) {
            // TODO(nil): @copypasta
            auto provider = new DocumentPartProvider();
            provider->hwnd = this->hwnd;
            provider->document_tree_ = this->document_tree_;
            provider->part_i = other_part_i;
            provider->AddRef();
            *pRetVal = provider;
        }
        return S_OK;
    }

    HRESULT SetFocus(void) override
    {
        if (part_i == 0) return S_OK;
        auto &document = *this->document_tree_;
        document.active_part_i = this->part_i;
        UiaRaiseAutomationEvent(this, UIA_AutomationFocusChangedEventId);
        return S_OK;
    }

    HRESULT get_BoundingRectangle(UiaRect *pRetVal) override
    {
        if (part_i == 0)
        {
            *pRetVal = {};
            return S_OK;
        }

        auto const& document = *this->document_tree_;
        auto const& part_i = this->part_i;
        auto const& content = document.parts[part_i].content;

        auto document_box = content.box;
        if (part_i == 0) {
            document_box = document.window_with_decoration;
        }

        auto const box = physical_screen_from_logical_window(
            hwnd,
            document_box);
        pRetVal->left = box.min.x;
        pRetVal->top = box.min.y;
        pRetVal->width = box.max.x - box.min.x;
        pRetVal->height = box.max.y - box.min.y;
        return S_OK;
    }

    HRESULT get_FragmentRoot(IRawElementProviderFragmentRoot **pRetVal) override
    {
        auto other_part_i = /* root */0;
        // TODO(nil): @copypasta
        auto provider = new DocumentPartProvider();
        provider->hwnd = this->hwnd;
        provider->document_tree_ = this->document_tree_;
        provider->part_i = other_part_i;
        provider->AddRef();
        *pRetVal = provider;
        return S_OK;
    }

    // IRawElementProviderFragmentRoot:
    HRESULT ElementProviderFromPoint(double x, double y,
                                     IRawElementProviderFragment **pRetVal)
    {
        auto const point = logical_point_from_physical_screen(
            this->hwnd,
            { (float)x, (float)y });
        auto const& document = *document_tree_;
        Float32Box2 smallest_box = box2_min_max_identity();
        int smallest_part_i = 0;

        // TODO(nil): disambiguates multiple colliding regions
        for (int part_i = 0; part_i < document.parts_n; ++part_i) {
            auto const &content = document.parts[part_i].content;
            if (point.x < content.box.min.x) continue;
            if (point.x > content.box.max.x) continue;
            if (point.y < content.box.min.y) continue;
            if (point.y > content.box.max.y) continue;
            smallest_part_i = part_i;
        }

        auto other_part_i = smallest_part_i;
        // TODO(nil): @copypasta
        auto provider = new DocumentPartProvider();
        provider->hwnd = this->hwnd;
        provider->document_tree_ = this->document_tree_;
        provider->part_i = other_part_i;
        provider->AddRef();
        *pRetVal = provider;
        return S_OK;
    }

    HRESULT GetFocus(IRawElementProviderFragment **pRetVal)
    {
        if (part_i == 0) {
            *pRetVal = nullptr;
            return S_OK;
        }
        auto const& document = *this->document_tree_;
        auto other_part_i = document.active_part_i;
        // TODO(nil): @copypasta
        auto provider = new DocumentPartProvider();
        provider->hwnd = this->hwnd;
        provider->document_tree_ = this->document_tree_;
        provider->part_i = other_part_i;
        provider->AddRef();
        *pRetVal = provider;
        return S_OK;
    }
};

static LRESULT UiAccessWindowProc(DocumentTree* document_,
                                  HWND hWnd,
                                  UINT uMsg,
                                  WPARAM wParam,
                                  LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_GETOBJECT: {
            const auto objectId = static_cast<DWORD>(lParam);
            switch (objectId) {
                case UiaRootObjectId: {
                    auto provider = new DocumentPartProvider();
                    provider->hwnd = hWnd;
                    provider->document_tree_ = document_;
                    provider->part_i = 0;
                    provider->AddRef();
                    auto result = UiaReturnRawElementProvider(
                        hWnd, wParam, lParam, provider);
                    provider->Release();
                    return result;
                } break;
            }
        } break;
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}


static DocumentTree global_document;

static void demo_document_init(DocumentTree* document_tree_)
{
    DocumentTree& document = *document_tree_;
    // Add synthetic root at index 0
    ++document.parts_n;
    auto &root = document.parts[0];
    root.content.text_utf8 = "demo_ui_access";
    root.content.text_utf8_n = 15;
    root.part_id = 0xd385da7adf5672bc;

    // Add container
    {
        auto part_i = document.parts_n++;
        root.children_index_first = part_i;
        root.children_index_last = part_i + 1;

        auto child1_part_i = document.parts_n++;
        auto child2_part_i = document.parts_n++;

        auto &container_part = document.parts[part_i];
        container_part.part_id = 0xb714eb61b5681418;
        container_part.children_index_first = child1_part_i;
        container_part.children_index_last = child2_part_i + 1;

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
                                  float min_x, float min_y,
                                  float max_x, float max_y)
{
    DocumentTree& document = *document_tree_;
    float x = min_x;
    float divisor = (float)(parts_l - parts_f);
    float x_inc = (max_x - min_x) / divisor;
    for (auto parts_i = parts_f; parts_i != parts_l; ++parts_i) {
        auto part_min_x = x;
        auto part_max_x = x + x_inc;
        auto &part = document.parts[parts_i];
        part.content.box.min.x = part_min_x;
        part.content.box.max.x = part_max_x;
        part.content.box.min.y = min_y;
        part.content.box.max.y = max_y;
        document_layout_horizontal_split(&document,
                                         part.children_index_first,
                                         part.children_index_last,
                                         part.content.box.min.x,
                                         part.content.box.min.y,
                                         part.content.box.max.x,
                                         part.content.box.max.y);
        x += x_inc;
    }
}

static void demo_document_layout(DocumentTree* document_tree_, Float32Box2 window, Float32Box2 window_with_decoration)
{
    DocumentTree& document = *document_tree_;

    document.window_with_decoration = window_with_decoration;
    auto min_x = window.min.x;
    auto min_y = window.min.y;
    auto max_x = window.max.x;
    auto max_y = window.max.y;

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
            RECT window_rect;
            GetWindowRect(hWnd, &window_rect);
            MapWindowPoints(HWND_DESKTOP, hWnd, (LPPOINT)(&window_rect), 2);
            int width = (lParam>>0) & 0xffff;
            int height = (lParam>>16) & 0xffff;
            Float32Box2 window_with_decorations = {
                { (float)window_rect.left, (float)window_rect.top },
                { (float)window_rect.right, (float)window_rect.bottom }
            };
            Float32Box2 window = {
                { (float)0, (float)0 },
                { (float)width, (float)height },
            };

            demo_document_layout(&global_document, window, window_with_decorations);
        } break;
    }
    return UiAccessWindowProc(&global_document, hWnd, uMsg, wParam, lParam);
}


extern "C" int WINAPI WinMain(
_In_ HINSTANCE hI,
_In_opt_ HINSTANCE hPI,
_In_ char* lpCmdLine,
_In_ int nCmdShow)
{
    // TODO(nicolas): did I forget CoInitialize?
    demo_document_init(&global_document);
    auto const application_name = L"demo_win32_ui_access";
    WNDCLASSEXW main_class = {};
    {
        auto& d = main_class;
        d.cbSize = sizeof(d);
        d.style = CS_VREDRAW | CS_HREDRAW;
        d.lpfnWndProc = main_window_proc;
        d.lpszClassName = L"demo_ui_access";
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

