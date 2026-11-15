#pragma once
// dd hardware demo scene: portable (Arduino + LovyanGFX + dd only). Used by FreenoveS3Demo and PicoWDemo;
// the two copies are kept identical. The hardware-specific parts (display class, pins, touch / encoder) live in main.cpp.
//
// The scene: a row of one-of-each shapes, nested groups (a solar system, a clock, a gear), a field of bouncing particles
// (culling + object churn) and an on-screen stats HUD. Shapes are draggable with touch (dd's ddInteract), or driven with an
// encoder + two buttons. The input hardware is NOT part of this file: main.cpp reads it (SuperGUI's SuperTouch / SuperEncoder /
// SuperButton in the two examples) and calls the input methods below:
//   touch:    Frame(down, x, y) every frame, plus MediumPress / LongPress / DoubleClick for the extra gestures
//   encoder:  EncoderTurn(steps), SetMode / NextMode / PrevMode
//   buttons:  ResetSelected, BringSelectedToFront, ToggleCull, ...
//
// Options (define before including, e.g. in platformio.ini build_flags):
//   DEMO_PARTICLES   how many bouncing particles (default 100)
//   DEMO_ARENA_BYTES byte cap of the scene's arena (default 128 KB)
//   DEMO_BUTTONS     1 = draw three on-screen touch buttons (+N, -N, cull); 0 = none (default 1)
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <dd.h>
#include <vector>

#ifndef DEMO_PARTICLES
#define DEMO_PARTICLES 100
#endif
#ifndef DEMO_ARENA_BYTES
#define DEMO_ARENA_BYTES (128u * 1024u)
#endif
#ifndef DEMO_BUTTONS
#define DEMO_BUTTONS 1
#endif

class DemoScene {
 public:
  enum Mode : uint8_t { M_SELECT, M_MOVE_X, M_MOVE_Y, M_ROTATE, M_SCALE, M_COUNT };

  LGFX_Sprite *Spr = nullptr;
  ddR *R = nullptr;
  RenderBlock *Scene = nullptr, *Overlay = nullptr;
  ddInteract *In = nullptr;
  size_t (*FreeHeap)() = nullptr;          // optional: set to report free heap on the HUD
  int  W = 0, H = 0;                       // display size in pixels
  float HW = 0, HH = 100.0f;               // world half-width / half-height (world is 200 units high, width follows the aspect)
  bool Cull = true;
  Mode ModeIdx = M_SELECT;
  float EmaFrameMs = 33.0f, EmaDrawMs = 10.0f;
  int  Touching = 0;                       // number of items the selection touches this frame
  uint32_t Frames = 0;

  // ---- setup -------------------------------------------------------------------------------
  // spr must already be created at lcdW x lcdH. Call once.
  void Begin(LGFX_Sprite *spr, int lcdW, int lcdH) {
    Spr = spr; W = lcdW; H = lcdH;
    HW = HH * (float)W / (float)H;
    R = new ddR(Spr, -HW, HW, -HH, HH);
    Scene = new RenderBlock(DEMO_ARENA_BYTES);
    Overlay = new RenderBlock(8u * 1024u);
    In = new ddInteract(R, Scene);
    In->OnEvent = &DemoScene::OnEvent;
    In->User = this;
    Build();
    AddParticles(DEMO_PARTICLES);
    LastMs = millis();
  }

  // ---- one frame: input, animate, draw into the sprite (the caller pushes it) ------------------
  // down / (px, py): the touch state in display pixels (pass false, 0, 0 when there is no touch).
  void Frame(bool down, float px, float py) {
    uint32_t t0 = micros();
    uint32_t nowMs = millis();
    float dt = (nowMs - LastMs) * 0.001f; LastMs = nowMs;
    if (dt > 0.1f) dt = 0.1f;
    float t = nowMs * 0.001f;
    Frames++;

    In->Update(down, ddP(px, py));
    R->CullEnabled = Cull;
    Animate(t, dt);

    Spr->clear();
    R->ResetStats();
    Scene->Draw(R);
    DrawCollisionOverlay();
    In->DrawSelection(TFT_WHITE);
    float drawMs = (micros() - t0) * 0.001f;
    EmaDrawMs += (drawMs - EmaDrawMs) * 0.1f;
    if (dt > 0.0f) EmaFrameMs += (dt * 1000.0f - EmaFrameMs) * 0.1f;
    DrawHud();
  }

  // ---- encoder / button control (no touch needed) ------------------------------------------------
  // Turn: in SELECT mode moves the selection (steps = detents); otherwise changes the selected item. The step size is what the
  // encoder reports, so an accelerating encoder (SuperEncoder: 1 / 5 / 20 per detent) moves, rotates and scales faster the faster you turn.
  void EncoderTurn(int steps) {
    if (!steps) return;
    if (ModeIdx == M_SELECT || !In->Selected) {
      int n = (int)Items.size();
      if (!n) return;
      SelIdx = ((SelIdx + steps) % n + n) % n;
      In->Selected = Items[SelIdx];
      return;
    }
    ddVectorPrimitive *s = In->Selected;
    const ddT &x = s->GetTransform();
    switch (ModeIdx) {
      case M_MOVE_X: s->Translate(steps * 1.0f, 0); break;                     // world units per step
      case M_MOVE_Y: s->Translate(0, steps * 1.0f); break;
      case M_ROTATE: s->SetAngle(x.Angle + steps * 0.05f); break;              // radians per step
      case M_SCALE: { float k = x.Sx + steps * 0.02f; if (k < 0.2f) k = 0.2f; if (k > 3.0f) k = 3.0f; s->SetScale(k); } break;
      default: break;
    }
  }
  bool HasSelection() const { return In && In->Selected != nullptr; }
  void SetMode(Mode m) { EnsureSelection(); ModeIdx = m; }
  void NextMode() { SetMode((Mode)((ModeIdx + 1) % M_COUNT)); }
  void PrevMode() { SetMode((Mode)((ModeIdx + M_COUNT - 1) % M_COUNT)); }
  void ButtonA() { NextMode(); }                      // kept for simple two-button setups: A = next mode
  // Resets the selected item's angle and scale; with nothing selected it selects the first item.
  void ResetSelected() {
    if (!In->Selected) { EnsureSelection(); return; }
    In->Selected->SetAngle(0); In->Selected->SetScale(1.0f);
  }
  void ButtonB() { ResetSelected(); }                 // kept for simple two-button setups: B = reset
  void BringSelectedToFront() { if (In->Selected && In->Selected->Tag == 0) Scene->MoveToFront(In->Selected); }
  void ToggleCull() {
    Cull = !Cull;
    if (cullLabel) cullLabel->SetText(Cull ? "cull on" : "cull off");
  }
  const char *ModeName() const {
    static const char *n[M_COUNT] = { "select", "move x", "move y", "rotate", "scale" };
    return n[ModeIdx];
  }

  // ---- extra touch gestures (from a gesture recognizer such as SuperTouch); coordinates are display pixels ----
  // Medium press on a shape: bring it to the front.   Long press: reset its angle and scale.   Double click anywhere: toggle culling.
  void MediumPress(float px, float py) { ddVectorPrimitive *p = At(px, py); if (p && p->Tag == 0) { In->Selected = p; Scene->MoveToFront(p); } }
  void LongPress(float px, float py)   { ddVectorPrimitive *p = At(px, py); if (p && p->Tag == 0) { In->Selected = p; p->SetAngle(0); p->SetScale(1.0f); } }
  void DoubleClick(float, float)       { ToggleCull(); }

  // ---- particles -----------------------------------------------------------------------------
  int ParticleCount() const { return (int)Parts.size(); }
  void AddParticles(int n) {
    for (int i = 0; i < n; i++) {
      ddVectorPrimitive *p = MakeParticle((int)(Rnd() * 5));
      if (!p) break;                                           // arena cap reached
      float a = Rnd() * 6.2832f, s = 6.0f + Rnd() * 24.0f;
      Parts.push_back({ p, cosf(a) * s, sinf(a) * s, (Rnd() - 0.5f) * 4.0f });
    }
  }
  void RemoveParticles(int n) {
    for (int i = 0; i < n && !Parts.empty(); i++) {
      In->Forget(Parts.back().P);
      Scene->Remove(Parts.back().P);
      Parts.pop_back();
    }
  }

 private:
  struct Particle { ddVectorPrimitive *P; float vx, vy, va; };
  std::vector<Particle> Parts;
  std::vector<ddVectorPrimitive *> Items;     // the shapes you can select / drag
  int SelIdx = 0;
  uint32_t LastMs = 0, RngState = 12345;
  ddRect *aRect = nullptr; ddNGon *aHex = nullptr; ddPolygon *aStar = nullptr, *aGear = nullptr;
  ddArc *aPie = nullptr; ddEllipse *aEll = nullptr;
  ddGroup *solar = nullptr, *clockG = nullptr, *planetA = nullptr, *planetB = nullptr;
  ddArrow *aMin = nullptr, *aHour = nullptr;
  enum { BTN_ADD = 1, BTN_SUB, BTN_CULL };
  ddText *cullLabel = nullptr;

  ddVectorPrimitive *At(float px, float py) {                 // the topmost shape under a display pixel (6 px of slop)
    return Scene->Pick(R->ToWorld(ddP(px, py)), 6.0f / R->PixelsPerUnit());
  }
  void EnsureSelection() { if (!In->Selected && !Items.empty()) In->Selected = Items[SelIdx]; }

  float Rnd() { RngState = RngState * 1664525u + 1013904223u; return (RngState >> 8) / 16777216.0f; }

  // particle field: the rectangle the particles bounce inside
  float FieldXLo() const { return -HW + 4.0f; }
  float FieldXHi() const { return HW - 4.0f; }
  static constexpr float FieldYLo = -82.0f, FieldYHi = -14.0f;

  ddVectorPrimitive *MakeParticle(int type) {
    float x = FieldXLo() + Rnd() * (FieldXHi() - FieldXLo()), y = FieldYLo + Rnd() * (FieldYHi - FieldYLo);
    uint16_t col = Spr->color565((uint8_t)(90 + Rnd() * 165), (uint8_t)(90 + Rnd() * 165), (uint8_t)(90 + Rnd() * 165));
    bool fill = Rnd() > 0.5f;
    ddVectorPrimitive *p = nullptr;
    switch (type % 5) {
      case 0: p = Scene->Emplace<ddCircle>(ddP(x, y), 1.5f + Rnd() * 2.5f, col, fill); break;
      case 1: p = Scene->Emplace<ddRect>(ddP(x, y), 3.0f + Rnd() * 4.0f, 2.0f + Rnd() * 3.0f, col, fill); break;
      case 2: p = Scene->Emplace<ddNGon>(ddP(x, y), 3 + (int)(Rnd() * 4), 2.0f + Rnd() * 2.5f, col, fill); break;
      case 3: p = Scene->Emplace<ddLine>(ddP(-3, 0), ddP(3, 0), col); if (p) p->SetPos(x, y); break;
      case 4: p = Scene->Emplace<ddPoint>(ddP(x, y), 1.0f, col); break;
    }
    if (p) p->Tag = 100;                                      // 100 = particle
    return p;
  }

  static void OnEvent(ddEvent ev, ddVectorPrimitive *p, const ddP &, void *user) {
    DemoScene *d = (DemoScene *)user;
    if (ev != ddEvent::Tap || !p || p->Tag == 0 || p->Tag >= 100) return;
    switch (p->Tag) {
      case BTN_ADD:  d->AddParticles(50); break;
      case BTN_SUB:  d->RemoveParticles(50); break;
      case BTN_CULL: d->ToggleCull(); break;
    }
  }

  void MakeButton(float x, int tag, const char *text, ddText **label = nullptr) {
    ddRect *bg = Scene->Emplace<ddRect>(ddP(x, -92), 34.0f, 10.0f, 0x18E3, true);
    ddRect *ol = Scene->Emplace<ddRect>(ddP(x, -92), 34.0f, 10.0f, TFT_DARKGREY);
    ddText *tx = Scene->Emplace<ddText>(ddP(x, -92), text, TFT_WHITE);
    if (bg) bg->Tag = tag;
    if (ol) ol->Tag = tag;
    if (tx) tx->Tag = tag;
    if (label) *label = tx;
  }

  void Item(ddVectorPrimitive *p) { if (p) { p->Draggable = true; Items.push_back(p); } }

  void Build() {
    Scene->Emplace<ddGrid>(20.0f, HW, HH);

    // row of one-of-each shapes
    const float y = 62.0f, gap = 2.0f * HW / 9.0f;
    auto X = [&](int i) { return -HW + gap * (i + 1); };
    aRect = Scene->Emplace<ddRect>(ddP(X(0), y), 15.0f, 10.0f, TFT_GREEN);                Item(aRect);
    Item(Scene->Emplace<ddCircle>(ddP(X(1), y), 8.0f, TFT_CYAN, true));
    aHex = Scene->Emplace<ddNGon>(ddP(X(2), y), 6, 9.0f, TFT_YELLOW, true);                Item(aHex);
    ddP star[10];
    for (int i = 0; i < 10; i++) { float a = DDD_DEG2RAD(90.0f + i * 36.0f), r = (i & 1) ? 4.0f : 10.0f; star[i].Set(r * cosf(a), r * sinf(a)); }
    aStar = Scene->EmplacePolygon(star, 10, TFT_CYAN, true, true);
    if (aStar) { aStar->SetPos(X(3), y); Item(aStar); }
    aPie = Scene->Emplace<ddArc>(ddP(X(4), y), 9.0f, 0.0f, 4.0f, TFT_ORANGE, true, true);  Item(aPie);
    ddBezier *bz = Scene->Emplace<ddBezier>(ddP(-9, -7), ddP(-3, 9), ddP(3, -9), ddP(9, 7), TFT_MAGENTA);
    if (bz) { bz->SetPos(X(5), y); bz->Pen.Width = 2; Item(bz); }
    ddArrow *ar = Scene->Emplace<ddArrow>(ddP(-8, -5), ddP(8, 5), TFT_RED, 5.0f, 4.0f, true);
    if (ar) { ar->SetPos(X(6), y); Item(ar); }
    aEll = Scene->Emplace<ddEllipse>(ddP(X(7), y), 9.0f, 5.0f, TFT_ORANGE);                Item(aEll);

    // nested groups
    solar = Scene->EmplaceGroup();
    if (solar) {
      solar->SetPos(-HW * 0.62f, 22.0f);
      solar->Children.Emplace<ddCircle>(ddP(0, 0), 4.0f, TFT_YELLOW, true);
      planetA = solar->Children.EmplaceGroup();
      if (planetA) {
        planetA->Children.Emplace<ddCircle>(ddP(12, 0), 2.5f, TFT_CYAN, true);
        planetB = planetA->Children.EmplaceGroup();                         // a moon orbiting planet A
        if (planetB) { planetB->SetPos(12, 0); planetB->Children.Emplace<ddCircle>(ddP(5, 0), 1.2f, TFT_WHITE, true); }
      }
      solar->Children.Emplace<ddCircle>(ddP(0, 0), 12.0f, TFT_DARKGREY);
      Item(solar);
    }
    clockG = Scene->EmplaceGroup();
    if (clockG) {
      clockG->SetPos(0, 22.0f);
      clockG->Children.Emplace<ddCircle>(ddP(0, 0), 16.0f, TFT_WHITE);
      for (int i = 0; i < 12; i++) {
        float a = DDD_DEG2RAD(i * 30.0f);
        clockG->Children.Emplace<ddLine>(ddP(13.0f * cosf(a), 13.0f * sinf(a)), ddP(16.0f * cosf(a), 16.0f * sinf(a)), TFT_WHITE);
      }
      aHour = clockG->Children.Emplace<ddArrow>(ddP(0, 0), ddP(0, 8), TFT_RED, 3.0f, 3.0f, true);
      aMin  = clockG->Children.Emplace<ddArrow>(ddP(0, 0), ddP(0, 13), TFT_GREEN, 3.0f, 2.5f, false);
      Item(clockG);
    }
    ddP gear[48];                                                            // a concave filled polygon
    for (int k = 0; k < 12; k++) {
      float a = DDD_DEG2RAD(k * 30.0f);
      const float off[4] = { -6, 6, 9, 21 }, rad[4] = { 17, 17, 13, 13 };
      for (int j = 0; j < 4; j++) gear[k * 4 + j].Set(rad[j] * cosf(a + DDD_DEG2RAD(off[j])), rad[j] * sinf(a + DDD_DEG2RAD(off[j])));
    }
    aGear = Scene->EmplacePolygon(gear, 48, 0x7BEF, true, true);
    if (aGear) { aGear->SetPos(HW * 0.62f, 22.0f); Item(aGear); }

#if DEMO_BUTTONS
    MakeButton(-42, BTN_ADD, "+50");
    MakeButton(0, BTN_SUB, "-50");
    MakeButton(42, BTN_CULL, "cull on", &cullLabel);
#endif
    if (!Items.empty()) SelIdx = 0;
  }

  void Animate(float t, float dt) {
    auto idle = [&](ddVectorPrimitive *p) { return p && p != In->Selected; };   // the selected item is held still so you can work on it
    if (idle(aRect)) aRect->SetAngle(t);
    if (idle(aHex)) aHex->SetAngle(-t * 2.0f);
    if (idle(aStar)) { aStar->SetAngle(t * 3.0f); }
    if (idle(aEll)) aEll->SetAngle(t * 2.0f);
    if (idle(aGear)) aGear->SetAngle(t);
    if (aPie) aPie->EndAngle = 2.0f + 3.0f * (0.5f + 0.5f * sinf(t * 2.0f));
    if (planetA) planetA->SetAngle(t * 2.0f);
    if (planetB) planetB->SetAngle(t * 6.0f);
    if (idle(solar)) solar->SetAngle(t * 0.5f);
    if (aMin) aMin->SetAngle(-t * 6.0f);
    if (aHour) aHour->SetAngle(-t * 0.5f);

    const float xl = FieldXLo(), xh = FieldXHi();
    for (Particle &p : Parts) {
      const ddT &x = p.P->GetTransform();
      float px = x.Tx + p.vx * dt, py = x.Ty + p.vy * dt;
      if (px < xl || px > xh) { p.vx = -p.vx; px = x.Tx; }
      if (py < FieldYLo || py > FieldYHi) { p.vy = -p.vy; py = x.Ty; }
      p.P->SetPos(px, py);
      p.P->SetAngle(x.Angle + p.va * dt);
    }
  }

  // Selected item: show the other items it touches, with their contact points.
  void DrawCollisionOverlay() {
    Overlay->Clear();
    Touching = 0;
    ddVectorPrimitive *sel = In->Selected;
    if (!sel || sel->Tag >= 100) return;
    for (ddVectorPrimitive *o : Items) {
      if (o == sel) continue;
      ddP a, b;
      int n = ddCollide(sel, o, &a, &b);
      if (n > 0) {
        Touching++;
        Overlay->Emplace<ddMarker>(a, ddMarkerType::Cross, 4, TFT_RED);
        if (n > 1) Overlay->Emplace<ddMarker>(b, ddMarkerType::Cross, 4, TFT_RED);
      } else if (ddOverlap(sel, o)) {
        Touching++;                                              // overlapping with no boundary contact: one inside the other
      }
    }
    Overlay->Draw(R);
  }

  void DrawHud() {
    char line[64];
    int fps = EmaFrameMs > 0.5f ? (int)(1000.0f / EmaFrameMs) : 0;
    snprintf(line, sizeof(line), "FPS %d  frame %d.%d ms  draw %d.%d ms", fps, (int)EmaFrameMs, (int)(EmaFrameMs * 10) % 10, (int)EmaDrawMs, (int)(EmaDrawMs * 10) % 10);
    R->DrawString(ddP(3, 2), line, TFT_GREEN, false);
    snprintf(line, sizeof(line), "obj %d drawn %u culled %u parts %d", Scene->Count(), (unsigned)R->Drawn, (unsigned)R->Culled, (int)Parts.size());
    R->DrawString(ddP(3, 11), line, TFT_GREEN, false);
    ddArena *A = Scene->Arena();
    if (FreeHeap) snprintf(line, sizeof(line), "arena %dK/%dK cap  heap %dK free", (int)(A->InUse() / 1024), (int)(A->Max() / 1024), (int)(FreeHeap() / 1024));
    else          snprintf(line, sizeof(line), "arena %dK of %dK cap%s", (int)(A->InUse() / 1024), (int)(A->Max() / 1024), A->Overflowed() ? " FULL" : "");
    R->DrawString(ddP(3, 20), line, A->Overflowed() ? TFT_RED : TFT_GREEN, false);
    snprintf(line, sizeof(line), "cull %s  mode %s  touching %d", Cull ? "on" : "off", ModeName(), Touching);
    R->DrawString(ddP(3, 29), line, TFT_GREEN, false);
  }
};
