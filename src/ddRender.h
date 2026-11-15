#pragma once
#include <LovyanGFX.hpp>
#include <cmath>
#include <vector>
#include "ddPoint.h"

// Portable millisecond delay (Arduino delay() on the boards, std::this_thread on the desktop).
#if defined(ARDUINO)
inline void delayMs(unsigned int ms) { delay(ms); }
#else
#include <chrono>
#include <thread>
inline void delayMs(unsigned int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
#endif

enum struct ddLineStyle : uint8_t { Solid, Dashed, Dotted };

// Stroke settings for line-based shapes. Sizes are in pixels.
struct ddPen {
  uint8_t Width = 1;
  ddLineStyle Style = ddLineStyle::Solid;
  uint8_t Dash = 6, Gap = 4;     // Dashed pattern; Dotted derives its pattern from Width
  bool Plain() const { return Width <= 1 && Style == ddLineStyle::Solid; }
};

class ddR {
  public:
  LGFX_Sprite *Sprite;
  float WorldXMin, WorldXMax, WorldYMin, WorldYMax;
  Matrix3x3 W2D, D2W;
  std::vector<ddP>   Tmp;   // reusable scratch: display-space vertices (grows once, then reused)
  std::vector<float> Xs;    // reusable scratch: scanline crossings

  // World rectangle covered by the sprite (valid after Refresh); used for culling.
  float ViewXMin = 0, ViewXMax = 0, ViewYMin = 0, ViewYMax = 0;
  bool  CullEnabled = true;
  uint32_t Drawn = 0, Culled = 0;     // per-frame stats; call ResetStats() at frame start
  void ResetStats() { Drawn = Culled = 0; }

  ddR(LGFX_Sprite *Spr, float WXMin, float WXMax, float WYMin, float WYMax)
    : Sprite(Spr), WorldXMin(WXMin), WorldXMax(WXMax), WorldYMin(WYMin), WorldYMax(WYMax) {
    Refresh();
  }

  ddP ToDisplay(const ddP &p) const { return W2D * p; }
  ddP ToWorld(const ddP &p)   const { return D2W * p; }

  // Pixels per world unit (uniform scale).
  float PixelsPerUnit() const { return fabsf(W2D.M[0][0]); }

  // True if the world-space box (grown by `padPx` pixels) overlaps the view.
  bool InView(float x0, float y0, float x1, float y1, float padPx = 3.0f) const {
    float pad = padPx / PixelsPerUnit();
    return !(x1 < ViewXMin - pad || x0 > ViewXMax + pad || y1 < ViewYMin - pad || y0 > ViewYMax + pad);
  }

  // Recompute world<->display matrices and the view rectangle
  // (call again if the sprite or world bounds change).
  void Refresh() {
    float ScaleX = Sprite->width()  / (WorldXMax - WorldXMin);
    float ScaleY = Sprite->height() / (WorldYMax - WorldYMin);
    float Scale  = (ScaleX < ScaleY) ? ScaleX : ScaleY;   // uniform, letterboxed

    float WorldCX  = (WorldXMin + WorldXMax) * 0.5f;
    float WorldCY  = (WorldYMin + WorldYMax) * 0.5f;
    float ScreenCX = Sprite->width()  * 0.5f;
    float ScreenCY = Sprite->height() * 0.5f;

    W2D.SetIdentity();
    W2D.SetScale(Scale, -Scale);   // flip Y: world up, screen down
    W2D.SetTranslation(ScreenCX - Scale*WorldCX, ScreenCY + Scale*WorldCY);

    D2W = W2D;
    D2W.Invert();

    ddP a = D2W * ddP(0.0f, 0.0f), b = D2W * ddP((float)Sprite->width(), (float)Sprite->height());
    ViewXMin = fminf(a[0], b[0]); ViewXMax = fmaxf(a[0], b[0]);
    ViewYMin = fminf(a[1], b[1]); ViewYMax = fmaxf(a[1], b[1]);
  }

  // ---- lines -------------------------------------------------------------------------
  // One solid segment in display coordinates, `width` pixels wide.
  void LinePx(float x0, float y0, float x1, float y1, uint16_t color, int width = 1) {
    if (width <= 1) Sprite->drawLine(lroundf(x0), lroundf(y0), lroundf(x1), lroundf(y1), color);
    else Sprite->drawWideLine(lroundf(x0), lroundf(y0), lroundf(x1), lroundf(y1), width * 0.5f, color);
  }

  // Pen-styled segment in display coordinates. `phase` carries the dash pattern across the
  // segments of a polyline (start it at 0).
  void StrokeSegment(float x0, float y0, float x1, float y1, uint16_t color, const ddPen &pen, float &phase) {
    if (pen.Style == ddLineStyle::Solid) { LinePx(x0, y0, x1, y1, color, pen.Width); return; }
    float dx = x1 - x0, dy = y1 - y0, len = hypotf(dx, dy);
    if (len < 0.001f) return;
    float dash = (pen.Style == ddLineStyle::Dotted) ? fmaxf(1.0f, (float)pen.Width) : (float)pen.Dash;
    float gap  = (pen.Style == ddLineStyle::Dotted) ? fmaxf(2.0f, pen.Width * 2.0f) : (float)pen.Gap;
    float period = dash + gap;
    if (period < 1.0f) period = 1.0f;
    float ux = dx / len, uy = dy / len, pos = 0.0f;
    while (pos < len) {
      bool on = phase < dash;
      float step = on ? dash - phase : period - phase;
      if (step < 0.01f) step = 0.01f;
      if (pos + step > len) step = len - pos;
      if (on) {
        if (step <= 1.5f && pen.Width <= 1)   // 1 px dot: a 2-point line would light two pixels
          Sprite->drawPixel(lroundf(x0 + ux * (pos + step * 0.5f)), lroundf(y0 + uy * (pos + step * 0.5f)), color);
        else LinePx(x0 + ux * pos, y0 + uy * pos, x0 + ux * (pos + step), y0 + uy * (pos + step), color, pen.Width);
      }
      pos += step; phase += step;
      if (phase >= period - 1e-4f) phase -= period;
    }
  }

  void DrawLine(ddP p1, ddP p2, uint16_t color, bool World = true, const ddPen &pen = ddPen()) {
    if (World) { p1 = W2D * p1; p2 = W2D * p2; }
    float phase = 0.0f;
    StrokeSegment(p1[0], p1[1], p2[0], p2[1], color, pen, phase);
  }

  // ---- text --------------------------------------------------------------------------
  void DrawString(ddP p, const char *str, uint16_t color, bool World = true) {
    if (World) p = W2D * p;
    Sprite->setTextColor(color);
    Sprite->setTextDatum(textdatum_t::top_left);
    Sprite->drawString(str, lroundf(p[0]), lroundf(p[1]), &fonts::Font0);
  }

  void DrawCenterString(ddP p, const char *str, uint16_t color, bool World = true) {
    if (World) p = W2D * p;
    Sprite->setTextColor(color);
    Sprite->setTextDatum(textdatum_t::middle_centre);
    Sprite->drawString(str, lroundf(p[0]), lroundf(p[1]), &fonts::Font0);
  }

  // ---- circles / ellipses (radii in pixels, centre in world units unless World=false) ----
  void DrawCircle(ddP c, int radius, uint16_t color, bool World = true) {
    if (World) c = W2D * c;
    Sprite->drawCircle(lroundf(c[0]), lroundf(c[1]), radius, color);
  }
  void DrawEllipse(ddP c, int rx, int ry, uint16_t color, bool World = true) {
    if (World) c = W2D * c;
    Sprite->drawEllipse(lroundf(c[0]), lroundf(c[1]), rx, ry, color);
  }
  void FillCircle(ddP c, int radius, uint16_t color, bool World = true) {
    if (World) c = W2D * c;
    Sprite->fillCircle(lroundf(c[0]), lroundf(c[1]), radius, color);
  }
  void FillEllipse(ddP c, int rx, int ry, uint16_t color, bool World = true) {
    if (World) c = W2D * c;
    Sprite->fillEllipse(lroundf(c[0]), lroundf(c[1]), rx, ry, color);
  }

  // ---- polygons ----------------------------------------------------------------------
  // Even-odd scanline fill of a polygon given in DISPLAY coordinates (convex or concave).
  void FillPolygonScreen(const ddP *p, int n, uint16_t color) {
    if (n < 3) return;
    if (n == 3) {
      Sprite->fillTriangle(lroundf(p[0][0]), lroundf(p[0][1]), lroundf(p[1][0]), lroundf(p[1][1]),
                           lroundf(p[2][0]), lroundf(p[2][1]), color);
      return;
    }
    float ymin = p[0][1], ymax = p[0][1];
    for (int i = 1; i < n; i++) { if (p[i][1] < ymin) ymin = p[i][1]; if (p[i][1] > ymax) ymax = p[i][1]; }
    int y0 = (int)ceilf(ymin - 0.5f), y1 = (int)floorf(ymax - 0.5f);
    if (y0 < 0) y0 = 0;
    if (y1 >= Sprite->height()) y1 = Sprite->height() - 1;
    for (int y = y0; y <= y1; y++) {
      float yc = y + 0.5f;
      Xs.clear();
      for (int i = 0, j = n - 1; i < n; j = i++) {
        const ddP &a = p[j], &b = p[i];
        if ((a[1] <= yc && b[1] > yc) || (b[1] <= yc && a[1] > yc))
          Xs.push_back(a[0] + (yc - a[1]) / (b[1] - a[1]) * (b[0] - a[0]));
      }
      for (size_t i = 1; i < Xs.size(); i++) {           // insertion sort (tiny lists)
        float v = Xs[i]; size_t k = i;
        while (k > 0 && Xs[k - 1] > v) { Xs[k] = Xs[k - 1]; k--; }
        Xs[k] = v;
      }
      for (size_t i = 0; i + 1 < Xs.size(); i += 2) {
        int xa = lroundf(Xs[i]), xb = lroundf(Xs[i + 1]);
        if (xb > xa) Sprite->drawFastHLine(xa, y, xb - xa, color);
      }
    }
  }

  // Polyline / polygon in DISPLAY coordinates. Filled shapes are treated as closed and ignore the pen.
  void DrawPolyScreen(const ddP *p, int n, bool closed, bool filled, uint16_t color, const ddPen &pen = ddPen()) {
    if (n < 2) return;
    if (filled) { FillPolygonScreen(p, n, color); return; }
    float phase = 0.0f;
    for (int i = 0; i + 1 < n; i++) StrokeSegment(p[i][0], p[i][1], p[i + 1][0], p[i + 1][1], color, pen, phase);
    if (closed && n > 2) StrokeSegment(p[n - 1][0], p[n - 1][1], p[0][0], p[0][1], color, pen, phase);
  }

  // Radius is in pixels. With DD_DEBUG and Label=true, the point's world coordinates are drawn above it.
  void DrawPoint(ddP p, int radius, uint16_t color, bool World = true, bool Label = true) {
    ddP Screen = World ? W2D * p : p;
    Sprite->fillCircle(lroundf(Screen[0]), lroundf(Screen[1]), radius, color);
#ifdef DD_DEBUG
    if (!Label) return;
    ddP WorldPos = World ? p : D2W * p;
    char buf[32];
    int Height = Sprite->fontHeight(&fonts::Font0);
    snprintf(buf, sizeof(buf), "(%.1f, %.1f)", (double)WorldPos[0], (double)WorldPos[1]);
    Sprite->setTextColor(color);
    Sprite->drawCenterString(buf, lroundf(Screen[0]), lroundf(Screen[1]) - radius - Height - Height/4, &fonts::Font0);
#else
    (void)Label;
#endif
  }
};
