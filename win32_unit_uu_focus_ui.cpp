#define WINVER 0x0601
#define _WIN32_WINNT 0x0601

#include "uu_focus_ui.hpp"

#include <D2d1.h>

UU_FOCUS_RENDER_UI_PROC(win32_uu_focus_ui_render)
{
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


