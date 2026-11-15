#pragma once
// Touch / pointer interaction on top of RenderBlock::Pick.
//  - Press selects the topmost primitive under the pointer (with a pixel-sized tolerance).
//  - Dragging moves primitives marked Draggable (top-level primitives of the scene).
//  - Tap = press + release without moving, quickly.
// Feed it once per frame:  interact.Update(touching, displayPos);
#include "ddVector.h"
#if !defined(ARDUINO)
#include <chrono>
#endif

enum struct ddEvent { Press, Drag, Release, Tap };
typedef void (*ddEventFn)(ddEvent ev, ddVectorPrimitive *p, const ddP &world, void *user);

inline uint32_t ddNowMs() {
#if defined(ARDUINO)
  return (uint32_t)millis();
#else
  using namespace std::chrono;
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
#endif
}

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

  ddInteract(ddR *r, RenderBlock *scene) : R(r), Scene(scene) {}

  // `down` = pointer/finger is down; `display` = position in display pixels.
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

  // Call before removing a primitive so Selected/Active don't dangle.
  void Forget(const ddVectorPrimitive *p) { if (Selected == p) Selected = nullptr; if (Active == p) Active = nullptr; }

  // Dashed outline around the selected primitive (if it has known bounds).
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
