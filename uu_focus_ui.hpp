#pragma once

struct ID2D1HwndRenderTarget;
struct ID2D1Factory;
struct IDWriteFactory;

#define UU_FOCUS_RENDER_UI_PROC(name_expr) \
  void name_expr(ID2D1HwndRenderTarget* rt, \
                 ID2D1Factory* d2d1factory, \
                 IDWriteFactory* dwritefactory)


