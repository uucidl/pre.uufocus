#define WINVER 0x0601
#define _WIN32_WINNT 0x0601

#include "uu_focus_ui.hpp"

#include "3rdparty/stb_image.h"

#include <D2d1.h>
#include <D2d1_1.h>

static void scale_centered(D2D1_RECT_F *d_rect_,
                           D2D1_RECT_F const& rect,
                           D2D1_RECT_F const& container_rect)
{
    auto &d_rect = *d_rect_;
    float x0 = rect.left + (rect.right - rect.left)/2.0f;
    float y0 = rect.top + (rect.bottom - rect.top)/2.0f;
    float x1 = container_rect.left + (container_rect.right - container_rect.left)/2.0f;
    float y1 = container_rect.top + (container_rect.bottom - container_rect.top)/2.0f;
    float ws = (rect.right - rect.left) / (container_rect.right - container_rect.left);
    float hs = (rect.bottom - rect.top) / (container_rect.bottom - container_rect.top);
    if (ws > hs) {
        d_rect.left = container_rect.left;
        d_rect.right = container_rect.right;
        d_rect.top = y1 + (rect.top - y0)/ws;
        d_rect.bottom = y1 + (rect.bottom - y0)/ws;
    } else {
        d_rect.left = x1 + (rect.left - x0)/hs;
        d_rect.right = x1 + (rect.right - x0)/hs;
        d_rect.top = container_rect.top;
        d_rect.bottom = container_rect.bottom;
    }
}

UU_FOCUS_RENDER_UI_PROC(win32_uu_focus_ui_render)
{
    static struct {
        stbi_uc* data;
        int size_x, size_y;
    } bg;
    if (!bg.data) {
        bg.data = stbi_load("C:\\Users\\nicolas\\Desktop\\assets for focus\\focus_bg5.jpeg",
                           &bg.size_x, &bg.size_y, 0, /* components */ 4);
    }

    /* image loading test */ if (bg.data) {
        D2D1_BITMAP_PROPERTIES properties = {};
        properties./*D2D1_PIXEL_FORMAT*/pixelFormat.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;

        HRESULT res;
        ID2D1Bitmap* bitmap;
        res = rt->CreateBitmap(
            D2D1_SIZE_U{(UINT32)bg.size_x, (UINT32)bg.size_y},
            bg.data,
            /* pitch */ (UINT32)bg.size_x*4,
            &properties,
            &bitmap);
        if (bitmap) {
            auto s_rect = D2D1::RectF(0.0f, 0.0f, float(bg.size_x), float(bg.size_y));
            auto size = rt->GetSize();
            auto screen_rect = D2D1::RectF(0.0f, 0.0f, size.width, size.height);
            D2D1_RECT_F d_rect;
            scale_centered(&d_rect, s_rect, screen_rect);
            rt->DrawBitmap(
                bitmap,
                &d_rect,
                1.0,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                &s_rect);
            bitmap->Release();
        }
    }

    /* path geometry test */ {
        ID2D1PathGeometry* _path_geometry;
        auto hr = d2d1factory->CreatePathGeometry(&_path_geometry);
        if (hr != S_OK) return;

        auto& pg = *_path_geometry;
        ID2D1GeometrySink* _path_geometry_sink;
        if (SUCCEEDED(pg.Open(&_path_geometry_sink))) {
            auto &sink = *_path_geometry_sink;
            sink.BeginFigure(D2D1::Point2F(0, 0),
                             D2D1_FIGURE_BEGIN_HOLLOW);
            sink.AddLine(D2D1::Point2F(80, 100));
            sink.EndFigure(D2D1_FIGURE_END_CLOSED);
            sink.Close();

            _path_geometry_sink->Release();
            _path_geometry_sink = nullptr;
        }

        ID2D1SolidColorBrush* fg_brush;
        rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.0f), &fg_brush);
        rt->DrawGeometry(_path_geometry, fg_brush);

        fg_brush->Release();

        _path_geometry->Release();
        _path_geometry = nullptr;
    }
    return;
}

#define STB_IMAGE_IMPLEMENTATION
#include "3rdparty/stb_image.h"
