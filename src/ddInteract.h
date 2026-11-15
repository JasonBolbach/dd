#pragma once
/**
 * @file ddInteract.h
 * Touch / pointer interaction on top of RenderBlock::Pick.
 *
 *   - A PRESS selects the topmost primitive under the pointer (with a pixel-sized tolerance).
 *   - DRAGGING moves primitives marked `Draggable` (top-level primitives of the scene).
 *   - A TAP is a quick press-and-release without moving.
 *
 * @code
 * ddInteract in(&renderer, &scene);
 * in.OnEvent = [](ddEvent e, ddVectorPrimitive *p, const ddP &world, void *) {
 *   if (e == ddEvent::Tap && p) onTapped(p);
 * };
 * shape->Draggable = true;
 *
 * void loop() {
 *   ddP t; bool down = readTouch(&t);        // display pixels
 *   in.Update(down, t);                      // once per frame
 *   scene.Draw(&renderer);
 *   in.DrawSelection();
 * }
 * @endcode
 *
 * @warning Feed Update() EVERY frame, including frames where nothing is touched, or releases and taps are never seen.
 * @note On the desktop, a click shorter than one frame is still seen when you read the mouse through ddSDLInput (it latches presses).
 */
#include "ddVector.h"
#if !defined(ARDUINO)
#include <chrono>
#endif

/**
 * What happened to a primitive: Press (pointer went down on it), Drag (moved while down, after passing the tap distance),
 * Release (pointer went up), Tap (a quick Release without movement; delivered after the Release).
 */
enum struct ddEvent { Press, Drag, Release, Tap };
/**
 * Event callback signature.
 * @param ev     The event.
 * @param p      The primitive the pointer pressed (never null for the events generated here).
 * @param world  Pointer position in world units.
 * @param user   The `User` pointer you set on the ddInteract.
 * @note A plain function pointer (no std::function), so it costs no heap on microcontrollers. A capture-less lambda converts to it.
 */
typedef void (*ddEventFn)(ddEvent ev, ddVectorPrimitive *p, const ddP &world, void *user);

/**
 * Millisecond clock: Arduino millis() on the boards, steady_clock on the desktop. Wraps after ~49 days; differences are what matter.
 */
inline uint32_t ddNowMs() {
#if defined(ARDUINO)
  return (uint32_t)millis();
#else
  using namespace std::chrono;
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
#endif
}

/**
 * Pointer interaction state machine: selection, dragging and taps over a scene.
 *
 * @par Fields
 *   - `OnEvent`, `User` callback and its user pointer. `PickTolPx` pick slop in pixels (default 4). `TapMaxPx` / `TapMaxMs` limits for a tap (6 px, 400 ms).
 *   - `Selected` the last pressed primitive (cleared by a tap on empty space). `Active` the primitive under the pointer right now.
 *
 * @warning `Selected` and `Active` are raw pointers. Call Forget(p) before removing a primitive so they do not dangle.
 * @warning Only TOP-LEVEL primitives of the scene are picked (a child of a group is picked as the group).
 */
class ddInteract {
 public:
  ddR *R;
  RenderBlock *Scene;
  ddEventFn OnEvent = nullptr;
  void *User = nullptr;
  float PickTolPx = 4.0f;     // pick slop in pixels
  float TapMaxPx  = 6.0f;     // max pointer travel for a tap
  uint32_t TapMaxMs = 400;    // max press duration for a tap
  ddVectorPrimitive *Selected = nullptr;   // last pressed primitive (cleared by a tap on empty space)
  ddVectorPrimitive *Active   = nullptr;   // primitive under the pointer right now

  /**
   * @param r      The renderer (used to convert display pixels to world units).
   * @param scene  The scene to pick from.
   */
  ddInteract(ddR *r, RenderBlock *scene) : R(r), Scene(scene) {}

  /**
   * Advances the interaction. Call once per frame.
   * @param down     true while the pointer / finger is down.
   * @param display  Pointer position in DISPLAY pixels (what a touch controller reports).
   * @note Events fire from inside this call (OnEvent), in this order on a press: Press; while held and moved: Drag; on release: Release then, for a tap, Tap.
   * @warning Dragging only moves primitives whose `Draggable` is true; Drag events fire either way.
   */
  void Update(bool down, const ddP &display) {
    ddP w = R->D2W * display;
    if (down && !Was) {
      PressPx = display; PressMs = ddNowMs(); Moved = false; LastW = w;
      Active = Scene->Pick(w, PickTolPx / R->PixelsPerUnit());
      if (Active) { Selected = Active; Fire(ddEvent::Press, w); }
    } else if (down && Was) {
      if (!Moved && display.DistanceTo(PressPx) > TapMaxPx) Moved = true;
      if (Active && Moved) {
        if (Active->Draggable) Active->Translate(w[0] - LastW[0], w[1] - LastW[1]);
        Fire(ddEvent::Drag, w);
      }
      LastW = w;
    } else if (!down && Was) {
      if (Active) {
        Fire(ddEvent::Release, w);
        if (!Moved && ddNowMs() - PressMs <= TapMaxMs) Fire(ddEvent::Tap, w);
      } else if (!Moved) Selected = nullptr;
      Active = nullptr;
    }
    Was = down;
  }

  /**
   * Clears `Selected` / `Active` if they point at p. Call it before RenderBlock::Remove(p).
   */
  void Forget(const ddVectorPrimitive *p) { if (Selected == p) Selected = nullptr; if (Active == p) Active = nullptr; }

  /**
   * Draws a dashed rectangle around the selected primitive (if it has known bounds). Call after drawing the scene.
   * @param color  RGB565 outline colour (default white).
   * @note The box is the primitive's axis-aligned world bounds, so a rotated shape gets a larger box than the shape.
   */
  void DrawSelection(uint16_t color = TFT_WHITE) {
    float x0, y0, x1, y1;
    if (!Selected || !Selected->ParentBounds(x0, y0, x1, y1)) return;
    ddPen pen; pen.Style = ddLineStyle::Dashed; pen.Dash = 4; pen.Gap = 3;
    ddP c[4] = { ddP(x0, y0), ddP(x1, y0), ddP(x1, y1), ddP(x0, y1) };
    for (int i = 0; i < 4; i++) R->DrawLine(c[i], c[(i + 1) & 3], color, true, pen);
  }

 private:
  bool Was = false, Moved = false;
  ddP PressPx, LastW;
  uint32_t PressMs = 0;
  void Fire(ddEvent e, const ddP &w) { if (OnEvent) OnEvent(e, Active, w, User); }
};
