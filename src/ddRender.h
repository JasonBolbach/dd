#pragma once
/**
 * @file ddRender.h
 * The renderer: world-to-display mapping, pens, polygon fill and per-frame culling statistics.
 *
 * ddR sits between your world coordinates (y up, any units you like) and a LovyanGFX sprite (pixels, y down). Scene
 * primitives (ddVector.h) draw THROUGH a ddR; you can also call its Draw* helpers directly for immediate-mode drawing.
 *
 * @note The only dependency is LovyanGFX. The display or sprite is yours: dd never creates one.
 * @note Everything is float. Pixel coordinates are rounded (lroundf), not truncated, so animated shapes do not jitter.
 */
#include <LovyanGFX.hpp>
#include <cmath>
#include <vector>
#include "ddPoint.h"

// Portable millisecond delay (Arduino delay() on the boards, std::this_thread on the desktop).
#if defined(ARDUINO)
/**
 * Portable millisecond delay: Arduino delay() on the boards, std::this_thread::sleep_for on the desktop.
 * @param ms  Milliseconds to wait.
 * @note Defined twice (once per branch of an #if); the Arduino one is shown.
 */
inline void delayMs(unsigned int ms) { delay(ms); }
#else
#include <chrono>
#include <thread>
inline void delayMs(unsigned int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
#endif

/**
 * Dash pattern of a ddPen: Solid, Dashed (pen.Dash / pen.Gap pixels) or Dotted (round dots sized from the pen width).
 */
enum struct ddLineStyle : uint8_t { Solid, Dashed, Dotted };

/**
 * Stroke settings for line-based shapes. All sizes are in PIXELS (they do not scale with the world).
 *
 * @code
 * ddLine *l = scene.Emplace<ddLine>(ddP(-50, 0), ddP(50, 0));
 * l->Pen.Width = 3;
 * l->Pen.Style = ddLineStyle::Dashed;
 * l->Pen.Dash = 8; l->Pen.Gap = 4;
 * @endcode
 *
 * @warning Wide pens use LovyanGFX's drawWideLine, which is noticeably slower than a 1-pixel line.
 * @warning A non-plain pen on a circle or rectangle disables the fast circle / rect paths and draws the shape as a
 *          polyline instead (still correct, a little slower).
 * @note Dotted with width 1 draws single pixels.
 */
struct ddPen {
  uint8_t Width = 1;
  ddLineStyle Style = ddLineStyle::Solid;
  uint8_t Dash = 6, Gap = 4;     // Dashed pattern; Dotted derives its pattern from Width
  /**
   * @return true for a 1-pixel solid pen. Shapes use this to decide whether LovyanGFX's fast primitives can be used.
   */
  bool Plain() const { return Width <= 1 && Style == ddLineStyle::Solid; }
};

/**
 * Renderer: maps a world rectangle onto a sprite and draws world-space primitives.
 *
 * The world rectangle is fitted into the sprite with ONE uniform scale (no distortion): if the aspect ratios differ the
 * world is centred with empty bars. World y points UP, display y points DOWN; ddR flips it.
 *
 * @code
 * LGFX_Sprite spr(&lcd); spr.createSprite(320, 240);
 * ddR r(&spr, -160, 160, -120, 120);          // 1 world unit = 1 pixel here, origin in the middle
 * ddP onScreen = r.ToDisplay(ddP(0, 0));      // (160, 120)
 * ddP inWorld  = r.ToWorld(ddP(160, 120));    // (0, 0)  - e.g. a touch point
 * @endcode
 *
 * @warning Holds a raw pointer to the sprite. The sprite must outlive the ddR, and if it is recreated at another size you
 *          must call Refresh().
 * @note Draw* functions take a `World` flag: true (default) = the point is in world units, false = already in display
 *       pixels. Mixing them up is the most common mistake: a shape at "(160,120)" in world units is far off-screen.
 */
class ddR {
  public:
  /**
   * The target sprite (public so primitives can call LovyanGFX directly).
   */
  LGFX_Sprite *Sprite;
  /**
   * The world rectangle that Refresh() fits to the sprite. Change them, then call Refresh().
   */
  float WorldXMin, WorldXMax, WorldYMin, WorldYMax;
  /**
   * W2D: world to display pixels. D2W: its inverse (display pixels to world, e.g. for touch). Valid after Refresh().
   */
  Matrix3x3 W2D, D2W;
  /**
   * Scratch vertex buffer shared by primitives while drawing (display-space points). Grows once, then is reused, so
   * drawing does no per-frame allocation. Do not hold references across a draw call.
   */
  std::vector<ddP>   Tmp;   // reusable scratch: display-space vertices (grows once, then reused)
  /**
   * Scratch buffer used by the polygon scanline fill.
   */
  std::vector<float> Xs;    // reusable scratch: scanline crossings

  /**
   * The WORLD rectangle actually covered by the sprite (after letterboxing). This is what culling tests against.
   * Valid after Refresh().
   */
  float ViewXMin = 0, ViewXMax = 0, ViewYMin = 0, ViewYMax = 0;
  /**
   * Master switch for culling. true (default): primitives with known bounds that lie outside the view are not drawn.
   * Turn it off to measure what culling saves, or if you draw outside the sprite on purpose.
   */
  bool  CullEnabled = true;
  /**
   * Per-frame statistics: primitives drawn / skipped by culling. Groups count themselves and their children separately.
   * Call ResetStats() at the start of each frame.
   */
  uint32_t Drawn = 0, Culled = 0;     // per-frame stats; call ResetStats() at frame start
  /**
   * Zeroes Drawn and Culled. Call once at the start of every frame.
   */
  void ResetStats() { Drawn = Culled = 0; }

  /**
   * Creates the renderer and computes the mappings (Refresh()).
   * @param Spr    The sprite (or any LGFX_Sprite-compatible target) to draw into.
   * @param WXMin  World x shown at the left edge (before letterboxing).
   * @param WXMax  World x at the right edge.
   * @param WYMin  World y at the BOTTOM edge.
   * @param WYMax  World y at the top edge.
   * @warning The sprite must already be created (non-zero size) when you construct the ddR.
   */
  ddR(LGFX_Sprite *Spr, float WXMin, float WXMax, float WYMin, float WYMax)
    : Sprite(Spr), WorldXMin(WXMin), WorldXMax(WXMax), WorldYMin(WYMin), WorldYMax(WYMax) {
    Refresh();
  }

  /**
   * Converts a world point to display pixels.
   * @return Pixel position (float; round it for drawing). Y is flipped.
   */
  ddP ToDisplay(const ddP &p) const { return W2D * p; }
  /**
   * Converts display pixels to a world point (for touch and mouse input).
   * @return World position.
   */
  ddP ToWorld(const ddP &p)   const { return D2W * p; }

  /**
   * @return The uniform scale: display pixels per world unit. Use it to convert world sizes to pixel sizes.
   */
  float PixelsPerUnit() const { return fabsf(W2D.M[0][0]); }

  /**
   * Culling test: does a world-space box overlap the visible area?
   * @param x0,y0,x1,y1  The box (min/max corners, world units).
   * @param padPx         Safety margin in pixels (default 3) so thick strokes at the edge are not culled.
   * @return true if the box overlaps the view (or touches the margin).
   */
  bool InView(float x0, float y0, float x1, float y1, float padPx = 3.0f) const {
    float pad = padPx / PixelsPerUnit();
    return !(x1 < ViewXMin - pad || x0 > ViewXMax + pad || y1 < ViewYMin - pad || y0 > ViewYMax + pad);
  }

  /**
   * Recomputes W2D, D2W and the view rectangle from the world bounds and the sprite's CURRENT size.
   * @note Called by the constructor. Call it again after changing WorldXMin..WorldYMax or recreating the sprite.
   * @warning Divides by the world width/height: a degenerate (zero-size) world rectangle gives infinities.
   */
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
  /**
   * Draws one solid segment in DISPLAY pixels.
   * @param width  Pixels; 1 uses the fast drawLine, more uses drawWideLine.
   */
  void LinePx(float x0, float y0, float x1, float y1, uint16_t color, int width = 1) {
    if (width <= 1) Sprite->drawLine(lroundf(x0), lroundf(y0), lroundf(x1), lroundf(y1), color);
    else Sprite->drawWideLine(lroundf(x0), lroundf(y0), lroundf(x1), lroundf(y1), width * 0.5f, color);
  }

  /**
   * Draws one pen-styled segment in DISPLAY pixels, honouring dashes and dots.
   * @param phase  In/out: position within the dash pattern. Pass the same variable to consecutive segments of a polyline so
   *               the pattern flows around corners; start it at 0.
   * @note This is the primitive every dashed shape is built on.
   */
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

  /**
   * Immediate-mode line.
   * @param p1,p2   End points.
   * @param color   RGB565 colour (TFT_* constants or lcd.color565()).
   * @param World   true: points are world units; false: display pixels.
   * @param pen     Width and dash style (default: 1-pixel solid).
   */
  void DrawLine(ddP p1, ddP p2, uint16_t color, bool World = true, const ddPen &pen = ddPen()) {
    if (World) { p1 = W2D * p1; p2 = W2D * p2; }
    float phase = 0.0f;
    StrokeSegment(p1[0], p1[1], p2[0], p2[1], color, pen, phase);
  }

  // ---- text --------------------------------------------------------------------------
  /**
   * Draws text with its top-left corner at p (built-in 6x8 Font0).
   * @param str  NUL-terminated text.
   * @note Text is not scaled by the world; it is always pixel-sized.
   */
  void DrawString(ddP p, const char *str, uint16_t color, bool World = true) {
    if (World) p = W2D * p;
    Sprite->setTextColor(color);
    Sprite->setTextDatum(textdatum_t::top_left);
    Sprite->drawString(str, lroundf(p[0]), lroundf(p[1]), &fonts::Font0);
  }

  /**
   * Draws text centred on p (built-in Font0). Same parameters as DrawString.
   */
  void DrawCenterString(ddP p, const char *str, uint16_t color, bool World = true) {
    if (World) p = W2D * p;
    Sprite->setTextColor(color);
    Sprite->setTextDatum(textdatum_t::middle_centre);
    Sprite->drawString(str, lroundf(p[0]), lroundf(p[1]), &fonts::Font0);
  }

  // ---- circles / ellipses (radii in pixels, centre in world units unless World=false) ----
  /**
   * Circle outline.
   * @param c       Centre (world units, or pixels if World is false).
   * @param radius  In PIXELS (not world units). Multiply a world radius by PixelsPerUnit() first.
   */
  void DrawCircle(ddP c, int radius, uint16_t color, bool World = true) {
    if (World) c = W2D * c;
    Sprite->drawCircle(lroundf(c[0]), lroundf(c[1]), radius, color);
  }
  /**
   * Axis-aligned ellipse outline. @param rx,ry Radii in PIXELS. For rotated ellipses use the ddEllipse primitive.
   */
  void DrawEllipse(ddP c, int rx, int ry, uint16_t color, bool World = true) {
    if (World) c = W2D * c;
    Sprite->drawEllipse(lroundf(c[0]), lroundf(c[1]), rx, ry, color);
  }
  /**
   * Filled disc. @param radius In PIXELS.
   */
  void FillCircle(ddP c, int radius, uint16_t color, bool World = true) {
    if (World) c = W2D * c;
    Sprite->fillCircle(lroundf(c[0]), lroundf(c[1]), radius, color);
  }
  /**
   * Filled axis-aligned ellipse. @param rx,ry Radii in PIXELS.
   */
  void FillEllipse(ddP c, int rx, int ry, uint16_t color, bool World = true) {
    if (World) c = W2D * c;
    Sprite->fillEllipse(lroundf(c[0]), lroundf(c[1]), rx, ry, color);
  }

  // ---- polygons ----------------------------------------------------------------------
  /**
   * Fills a polygon given in DISPLAY pixels using an even-odd scanline fill (convex or concave, self-intersections leave holes).
   * @param p      Vertices.
   * @param n      Vertex count (< 3 draws nothing; exactly 3 uses LovyanGFX's triangle fill).
   * @param color  RGB565 fill colour.
   * @note Uses the Xs scratch buffer: no allocation after the first call.
   */
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

  /**
   * Draws a polyline or polygon given in DISPLAY pixels.
   * @param p       Vertices.
   * @param n       Vertex count.
   * @param closed  true: also connect the last vertex back to the first.
   * @param filled  true: fill instead (implies closed; the pen is ignored).
   * @param pen     Stroke for outlines.
   * @note The dash pattern continues across vertices and around the closing edge.
   */
  void DrawPolyScreen(const ddP *p, int n, bool closed, bool filled, uint16_t color, const ddPen &pen = ddPen()) {
    if (n < 2) return;
    if (filled) { FillPolygonScreen(p, n, color); return; }
    float phase = 0.0f;
    for (int i = 0; i + 1 < n; i++) StrokeSegment(p[i][0], p[i][1], p[i + 1][0], p[i + 1][1], color, pen, phase);
    if (closed && n > 2) StrokeSegment(p[n - 1][0], p[n - 1][1], p[0][0], p[0][1], color, pen, phase);
  }

  /**
   * Filled dot, optionally labelled with its world coordinates.
   * @param radius  In PIXELS.
   * @param Label   Draws "(x, y)" above the dot. Only honoured when DD_DEBUG is defined (it needs float printf).
   * @warning Labels allocate a stack buffer and format floats every frame: leave them off in production.
   */
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
