#pragma once
/**
 * @file ddSDLInput.h
 * Desktop (SDL) pointer input in LOGICAL pixels, matched to LovyanGFX's SDL panel. Desktop builds only (needs SDL2).
 *
 * @par Why
 *   The SDL panel does not stretch its frame to the window: it scales it in 0.5 steps (floor(2*out/frame)/2) and CENTRES it, so a larger window shows bars
 *   around the image, and on hi-dpi displays the renderer output is bigger than the window in points. `lcd.getTouch()` maps the mouse across the WHOLE
 *   window, so it drifts as soon as bars appear. Here the mouse is mapped with exactly the panel's maths, and presses that land in the bars are ignored.
 *
 * @par Aspect lock (optional)
 *   When the window is resized it is snapped back to the frame's aspect ratio, so the panel's independent X/Y scale steps stay equal and the image is never stretched.
 *
 * @code
 * void setup() { lcd.init(); ddSDLInput::Begin(LCD_W, LCD_H); }
 * void loop()  { float x, y; bool down = ddSDLInput::Get(x, y); }   // left button, logical pixels
 * @endcode
 *
 * @note The mapping is ported from SuperGUI's Form Designer (DesignerInput.h).
 * @warning Use it under `#ifdef LGFX_SDL`; on real hardware read the touch controller instead.
 * @warning Rotation of the panel (the L/R keys) is not accounted for.
 */
#include <SDL.h>
#include <atomic>
#include <cmath>

/**
 * Static helper: install once, then poll the mouse. All state is static (one window).
 */
class ddSDLInput {
 public:
  /**
   * Installs the SDL event watch. Call once after the display is created.
   * @param logicalW,logicalH  The panel's frame size in pixels (e.g. the numbers you gave `LGFX(w, h)`).
   * @param lockAspect         true (default): keep the window at the frame's aspect ratio when it is resized.
   * @warning Call it with the SAME size as the panel, or clicks will land in the wrong place.
   */
  static void Begin(int logicalW, int logicalH, bool lockAspect = true) {
    LW = logicalW; LH = logicalH; Lock = lockAspect;
    if (!Added) { SDL_AddEventWatch(Watch, nullptr); Added = true; }
  }

  /**
   * Left-button state and position in LOGICAL pixels (the panel's pixel grid).
   * @param x,y  Outputs: the last pointer position. After a drag leaves the image it can be negative or >= the logical size.
   * @return true while the left button is down. A click shorter than one frame is LATCHED and reported as down once, so quick taps are not lost.
   * @note A press that starts in the bars (outside the image) is ignored.
   */
  static bool Get(float &x, float &y) {
    if (Down.load()) { x = (float)X.load(); y = (float)Y.load(); Latched = false; return true; }
    if (Latched.exchange(false)) { x = (float)PX.load(); y = (float)PY.load(); return true; }
    x = (float)X.load(); y = (float)Y.load();
    return false;
  }

  /**
   * Pure mapping from a window point to a logical pixel (no SDL calls; unit-tested).
   * @param wx,wy    Mouse position in window points.
   * @param ww,wh    Window size in points.
   * @param rw,rh    Renderer output size in pixels (larger than the window on hi-dpi displays).
   * @param lw,lh    Logical frame size.
   * @param lx,ly    Outputs: logical pixel. Outside 0..lw-1 / 0..lh-1 means the pointer is in a bar.
   * @note Uses integer division on purpose: it must match the panel's 0.5-step scale exactly.
   */
  static void Scale(int wx, int wy, int ww, int wh, int rw, int rh, int lw, int lh, int &lx, int &ly) {
    if (ww < 1) ww = 1;
    if (wh < 1) wh = 1;
    float sx = (rw * 2 / lw) / 2.0f, sy = (rh * 2 / lh) / 2.0f;     // integer division on purpose: 0.5 steps
    if (sx <= 0) sx = 0.5f;
    if (sy <= 0) sy = 0.5f;
    float px = (float)wx * rw / ww, py = (float)wy * rh / wh;         // point -> output pixel
    float ox = (rw - lw * sx) / 2.0f, oy = (rh - lh * sy) / 2.0f;     // where the image starts (centred)
    lx = (int)std::floor((px - ox) / sx);
    ly = (int)std::floor((py - oy) / sy);
  }

  /**
   * Pure helper for the aspect lock: the window size that has the frame's aspect.
   * @param w,h        The new window size.
   * @param prevW,prevH  The previous size. Whichever dimension moved most drives the other.
   * @param lw,lh      Logical frame size.
   * @param nw,nh      Outputs: the size to snap to.
   */
  static void AspectFit(int w, int h, int prevW, int prevH, int lw, int lh, int &nw, int &nh) {
    bool widthMoved = std::abs(w - prevW) >= std::abs(h - prevH);
    if (widthMoved) { nw = w; nh = (int)std::lround((double)w * lh / lw); }
    else            { nh = h; nw = (int)std::lround((double)h * lw / lh); }
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
  }

 private:
  inline static int LW = 1, LH = 1;
  inline static bool Lock = true, Added = false;
  inline static std::atomic<int> X{0}, Y{0};
  inline static std::atomic<int> PX{0}, PY{0};             // where the last press landed
  inline static std::atomic<bool> Down{false}, Latched{false};
  inline static int PrevW = 0, PrevH = 0;

  static void ToLogical(SDL_Window *win, int wx, int wy, int &lx, int &ly) {
    int ww = 1, wh = 1, rw = 1, rh = 1;
    if (win) {
      SDL_GetWindowSize(win, &ww, &wh); rw = ww; rh = wh;
      if (SDL_Renderer *r = SDL_GetRenderer(win)) SDL_GetRendererOutputSize(r, &rw, &rh);
    }
    Scale(wx, wy, ww, wh, rw, rh, LW, LH, lx, ly);
  }

  static int Watch(void *, SDL_Event *e) {
    switch (e->type) {
      case SDL_MOUSEMOTION: {
        int lx, ly; ToLogical(SDL_GetWindowFromID(e->motion.windowID), e->motion.x, e->motion.y, lx, ly);
        X = lx; Y = ly;
        break;
      }
      case SDL_MOUSEBUTTONDOWN:
      case SDL_MOUSEBUTTONUP: {
        if (e->button.button != SDL_BUTTON_LEFT) break;
        int lx, ly; ToLogical(SDL_GetWindowFromID(e->button.windowID), e->button.x, e->button.y, lx, ly);
        X = lx; Y = ly;
        bool inside = lx >= 0 && ly >= 0 && lx < LW && ly < LH;
        if (e->type == SDL_MOUSEBUTTONDOWN) { Down = inside; if (inside) { PX = lx; PY = ly; Latched = true; } }
        else Down = false;
        break;
      }
      case SDL_WINDOWEVENT:
        if (Lock && e->window.event == SDL_WINDOWEVENT_RESIZED) {
          SDL_Window *win = SDL_GetWindowFromID(e->window.windowID);
          int w = e->window.data1, h = e->window.data2;
          if (win && w > 0 && h > 0) {
            if (PrevW == 0) { PrevW = w; PrevH = h; }
            int nw, nh;
            AspectFit(w, h, PrevW, PrevH, LW, LH, nw, nh);
            if (std::abs(nw - w) > 1 || std::abs(nh - h) > 1) SDL_SetWindowSize(win, nw, nh);   // snaps back to the frame's aspect
            PrevW = nw; PrevH = nh;
          }
        }
        break;
    }
    return 0;
  }
};
