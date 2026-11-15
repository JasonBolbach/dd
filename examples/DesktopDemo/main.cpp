// dd* showcase + stress test.
//  - Top: one of each primitive (drag them with the mouse/touch), pen styles, nested groups.
//  - Middle/bottom: a field of moving particles (culling, transforms, churn).
//  - Buttons (tap): +500 / -500 particles, culling on/off, churn mode, clear.
//  - HUD: frame time, draw time, drawn/culled counts, arena usage.
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
#include <vector>

#include <dd.h>
#ifdef LGFX_SDL
#include "ddSDLInput.h"   // mouse mapped to the panel's scaling (bars, hi-dpi) + aspect lock
#endif

// Display size. 172x320 matches the small touch LCD; this test uses 4:3 so the 320x240 world fills it.
constexpr int LCD_W = 1280, LCD_H = 960;

LGFX *lcd0 = nullptr;
LGFX_Sprite *spr0 = nullptr;
LGFX_Sprite *img = nullptr;
ddR *r = nullptr;

RenderBlock scene(32u * 1024u * 1024u);   // retained: built once, objects modified every frame
RenderBlock overlay(64u * 1024u);         // per-frame: Clear() each frame reuses the same memory
ddInteract *interact = nullptr;

// ---- particle field ---------------------------------------------------------------------
struct Particle { ddVectorPrimitive *P; float vx, vy, va; };
std::vector<Particle> parts;
unsigned rngState = 12345;
float rnd() { rngState = rngState * 1664525u + 1013904223u; return (rngState >> 8) / 16777216.0f; }

constexpr float FIELD_X = 155.0f, FIELD_YLO = -105.0f, FIELD_YHI = 15.0f;
int churnMode = 0;                         // 0 off, 1 = 25 swaps/frame, 2 = 100 swaps/frame
const int churnRate[3] = { 0, 25, 100 };

ddVectorPrimitive *MakeParticle(int type) {
  float x = (rnd() * 2.0f - 1.0f) * FIELD_X, y = FIELD_YLO + rnd() * (FIELD_YHI - FIELD_YLO);
  uint16_t col = lcd0->color565((uint8_t)(80 + rnd() * 175), (uint8_t)(80 + rnd() * 175), (uint8_t)(80 + rnd() * 175));
  bool fill = rnd() > 0.5f;
  ddVectorPrimitive *p = nullptr;
  switch (type % 6) {
    case 0: p = scene.Emplace<ddCircle>(ddP(x, y), 1.0f + rnd() * 2.5f, col, fill); break;
    case 1: p = scene.Emplace<ddRect>(ddP(x, y), 2.0f + rnd() * 4.0f, 2.0f + rnd() * 3.0f, col, fill); break;
    case 2: p = scene.Emplace<ddNGon>(ddP(x, y), 3 + (int)(rnd() * 5), 1.5f + rnd() * 2.5f, col, fill); break;
    case 3: p = scene.Emplace<ddLine>(ddP(-2.5f, 0), ddP(2.5f, 0), col); if (p) p->SetPos(x, y); break;
    case 4: p = scene.Emplace<ddPoint>(ddP(x, y), 0.6f, col); break;
    case 5: p = scene.Emplace<ddEllipse>(ddP(x, y), 1.5f + rnd() * 2.5f, 1.0f + rnd() * 1.5f, col, fill); break;
  }
  if (p) p->Tag = (uint16_t)(100 + type % 6);   // 100+: particle kind
  return p;
}
void AddParticles(int n) {
  for (int i = 0; i < n; i++) {
    ddVectorPrimitive *p = MakeParticle((int)(rnd() * 6));
    if (!p) break;                              // arena cap reached
    float a = rnd() * 6.2832f, s = 6.0f + rnd() * 30.0f;
    parts.push_back({ p, cosf(a) * s, sinf(a) * s, (rnd() - 0.5f) * 4.0f });
  }
}
void RemoveParticles(int n) {
  for (int i = 0; i < n && !parts.empty(); i++) {
    interact->Forget(parts.back().P);
    scene.Remove(parts.back().P);
    parts.pop_back();
  }
}

// ---- buttons ------------------------------------------------------------------------------
enum { BTN_ADD = 1, BTN_SUB, BTN_CULL, BTN_CHURN, BTN_CLEAR };
ddText *cullLabel, *churnLabel;

void MakeButton(float x, int tag, const char *text, ddText **labelOut = nullptr) {
  ddRect *bg = scene.Emplace<ddRect>(ddP(x, -113), 30.0f, 9.0f, 0x18E3, true);
  ddRect *ol = scene.Emplace<ddRect>(ddP(x, -113), 30.0f, 9.0f, TFT_DARKGREY);
  ddText *tx = scene.Emplace<ddText>(ddP(x, -113), text, TFT_WHITE);
  if (bg) bg->Tag = tag;
  if (ol) ol->Tag = tag;
  if (tx) tx->Tag = tag;
  if (labelOut) *labelOut = tx;
}

void OnEvent(ddEvent ev, ddVectorPrimitive *p, const ddP &, void *) {
  if (ev != ddEvent::Tap || !p || p->Tag == 0 || p->Tag >= 100) return;
  switch (p->Tag) {
    case BTN_ADD:   AddParticles(500); break;
    case BTN_SUB:   RemoveParticles(500); break;
    case BTN_CULL:  r->CullEnabled = !r->CullEnabled; cullLabel->SetText(r->CullEnabled ? "cull ON" : "cull OFF"); break;
    case BTN_CHURN: churnMode = (churnMode + 1) % 3; { char b[16]; snprintf(b, sizeof(b), "churn %d", churnRate[churnMode]); churnLabel->SetText(b); } break;
    case BTN_CLEAR: RemoveParticles((int)parts.size()); break;
  }
}

// ---- showcase objects that get animated ------------------------------------------------------
ddRect *aRect; ddNGon *aHex; ddPolygon *aStar, *aGear; ddArc *aPie, *aGauge; ddEllipse *aEllipse; ddSprite *aSprite;
ddGroup *solar, *clockGrp; ddArrow *aHour, *aMin; ddGroup *planetA, *planetB;
ddBezier *aCurve;

void BuildShowcase() {
  scene.Emplace<ddGrid>(20.0f, 160.0f, 120.0f);

  const char *names[] = { "rect", "fill", "circle", "disc", "ellipse", "hex", "star", "pie", "bezier", "arrow", "marker", "sprite", "polyline", "text" };
  for (int i = 0; i < 14; i++) scene.Emplace<ddText>(ddP(-143.0f + 22.0f * i, 86.0f), names[i], TFT_DARKGREY);
  auto X = [](int i) { return -143.0f + 22.0f * i; };
  float y = 100.0f;
  ddVectorPrimitive *d;

  d = aRect = scene.Emplace<ddRect>(ddP(X(0), y), 16.0f, 11.0f, TFT_GREEN);            d->Draggable = true;
  d = scene.Emplace<ddRect>(ddP(X(1), y), 16.0f, 11.0f, TFT_GREEN, true);               d->Draggable = true;
  d = scene.Emplace<ddCircle>(ddP(X(2), y), 8.0f, TFT_CYAN);                            d->Draggable = true;
  d = scene.Emplace<ddCircle>(ddP(X(3), y), 8.0f, TFT_CYAN, true);                      d->Draggable = true;
  d = aEllipse = scene.Emplace<ddEllipse>(ddP(X(4), y), 9.0f, 5.0f, TFT_ORANGE);        d->Draggable = true;
  d = aHex = scene.Emplace<ddNGon>(ddP(X(5), y), 6, 8.0f, TFT_YELLOW, true);            d->Draggable = true;

  ddP star[10];
  for (int i = 0; i < 10; i++) { float a = DDD_DEG2RAD(90.0f + i * 36.0f), rad = (i & 1) ? 4.0f : 10.0f; star[i].Set(rad * cosf(a), rad * sinf(a)); }
  d = aStar = scene.EmplacePolygon(star, 10, TFT_CYAN, true, true); aStar->SetPos(X(6), y); d->Draggable = true;

  d = aPie = scene.Emplace<ddArc>(ddP(X(7), y), 9.0f, 0.0f, 4.0f, TFT_ORANGE, true, true); d->Draggable = true;
  d = aCurve = scene.Emplace<ddBezier>(ddP(-9, -7), ddP(-3, 9), ddP(3, -9), ddP(9, 7), TFT_MAGENTA); aCurve->SetPos(X(8), y); aCurve->Pen.Width = 2; d->Draggable = true;
  d = scene.Emplace<ddArrow>(ddP(-8, -5), ddP(8, 5), TFT_RED, 5.0f, 4.0f, true); d->SetPos(X(9), y); d->Draggable = true;

  ddGroup *mk = scene.EmplaceGroup(); mk->SetPos(X(10), y); mk->Draggable = true;
  const ddMarkerType types[5] = { ddMarkerType::Plus, ddMarkerType::Cross, ddMarkerType::Diamond, ddMarkerType::Square, ddMarkerType::Circle };
  for (int i = 0; i < 5; i++) mk->Children.Emplace<ddMarker>(ddP(-6.0f + (i % 3) * 6.0f, (i < 3) ? 3.0f : -3.0f), types[i], 4, TFT_WHITE, i >= 2);

  aSprite = scene.Emplace<ddSprite>(ddP(X(11), y), img, 0.4f); aSprite->HasTransparent = true; aSprite->Transparent = TFT_MAGENTA; aSprite->Draggable = true;

  ddP zig[6] = { {-9, -4}, {-5, 4}, {-1, -4}, {3, 4}, {7, -4}, {10, 2} };
  d = scene.EmplacePolygon(zig, 6, TFT_GREENYELLOW, false, false); d->SetPos(X(12), y); d->Pen.Width = 3; d->Draggable = true;
  d = scene.Emplace<ddText>(ddP(X(13), y), "dd text", TFT_WHITE);                         d->Draggable = true;

  // pen styles
  y = 72.0f;
  struct PenSpec { uint8_t w; ddLineStyle s; } pens[8] = {
    { 1, ddLineStyle::Solid }, { 3, ddLineStyle::Solid }, { 6, ddLineStyle::Solid }, { 1, ddLineStyle::Dashed },
    { 3, ddLineStyle::Dashed }, { 1, ddLineStyle::Dotted }, { 4, ddLineStyle::Dotted }, { 2, ddLineStyle::Dashed } };
  for (int i = 0; i < 8; i++) {
    float x0 = -150.0f + 38.0f * i;
    ddLine *l = scene.Emplace<ddLine>(ddP(x0, y), ddP(x0 + 30.0f, y), TFT_WHITE);
    l->Pen.Width = pens[i].w; l->Pen.Style = pens[i].s;
  }
  ddCircle *dc = scene.Emplace<ddCircle>(ddP(-130, 60), 6.0f, TFT_WHITE); dc->Pen.Style = ddLineStyle::Dashed; dc->Pen.Width = 2;
  ddRect *dr = scene.Emplace<ddRect>(ddP(-110, 60), 14.0f, 9.0f, TFT_WHITE); dr->Pen.Style = ddLineStyle::Dotted; dr->Pen.Width = 2;

  // nested groups: solar system
  solar = scene.EmplaceGroup(); solar->SetPos(-110, 38);
  solar->Children.Emplace<ddCircle>(ddP(0, 0), 4.0f, TFT_YELLOW, true);
  planetA = solar->Children.EmplaceGroup(); planetA->SetPos(0, 0);
  planetA->Children.Emplace<ddCircle>(ddP(12, 0), 2.5f, TFT_CYAN, true);
  planetB = planetA->Children.EmplaceGroup(); planetB->SetPos(12, 0);                 // moon orbits planet A
  planetB->Children.Emplace<ddCircle>(ddP(5, 0), 1.2f, TFT_WHITE, true);
  solar->Children.Emplace<ddCircle>(ddP(0, 0), 12.0f, TFT_DARKGREY);
  planetA->Children.Emplace<ddEllipse>(ddP(0, 0), 20.0f, 20.0f, 0x2104);

  // clock
  clockGrp = scene.EmplaceGroup(); clockGrp->SetPos(-40, 38);
  clockGrp->Children.Emplace<ddCircle>(ddP(0, 0), 16.0f, TFT_WHITE);
  for (int i = 0; i < 12; i++) {
    float a = DDD_DEG2RAD(i * 30.0f);
    clockGrp->Children.Emplace<ddLine>(ddP(13.0f * cosf(a), 13.0f * sinf(a)), ddP(16.0f * cosf(a), 16.0f * sinf(a)), TFT_WHITE);
  }
  aHour = clockGrp->Children.Emplace<ddArrow>(ddP(0, 0), ddP(0, 8), TFT_RED, 3.0f, 3.0f, true);
  aMin  = clockGrp->Children.Emplace<ddArrow>(ddP(0, 0), ddP(0, 13), TFT_GREEN, 3.0f, 2.5f, false);
  aMin->Pen.Width = 2;

  // gear (concave filled polygon)
  ddP gear[48];
  for (int t = 0; t < 12; t++) {
    float a = DDD_DEG2RAD(t * 30.0f);
    gear[t * 4 + 0].Set(17.0f * cosf(a + DDD_DEG2RAD(-6)), 17.0f * sinf(a + DDD_DEG2RAD(-6)));
    gear[t * 4 + 1].Set(17.0f * cosf(a + DDD_DEG2RAD( 6)), 17.0f * sinf(a + DDD_DEG2RAD( 6)));
    gear[t * 4 + 2].Set(13.0f * cosf(a + DDD_DEG2RAD( 9)), 13.0f * sinf(a + DDD_DEG2RAD( 9)));
    gear[t * 4 + 3].Set(13.0f * cosf(a + DDD_DEG2RAD(21)), 13.0f * sinf(a + DDD_DEG2RAD(21)));
  }
  aGear = scene.EmplacePolygon(gear, 48, 0x7BEF, true, true); aGear->SetPos(30, 38);
  scene.Emplace<ddCircle>(ddP(30, 38), 4.0f, TFT_BLACK, true);

  // gauge
  scene.Emplace<ddArc>(ddP(100, 30), 20.0f, DDD_DEG2RAD(210), DDD_DEG2RAD(-30), TFT_DARKGREY)->Pen.Width = 5;
  aGauge = scene.Emplace<ddArc>(ddP(100, 30), 20.0f, DDD_DEG2RAD(210), DDD_DEG2RAD(100), TFT_GREEN);
  aGauge->Pen.Width = 5;
  scene.Emplace<ddText>(ddP(100, 22), "gauge", TFT_WHITE);

  // buttons
  MakeButton(-120, BTN_ADD, "+500");
  MakeButton(-84, BTN_SUB, "-500");
  MakeButton(-48, BTN_CULL, "cull ON", &cullLabel);
  MakeButton(-12, BTN_CHURN, "churn 0", &churnLabel);
  MakeButton(24, BTN_CLEAR, "clear");

  // far-away static objects: never visible, only cost culling checks
  for (int i = 0; i < 1000; i++) {
    float x = 200.0f + rnd() * 600.0f, yy = (rnd() - 0.5f) * 600.0f;
    if (i & 1) x = -x;
    scene.Emplace<ddCircle>(ddP(x, yy), 3.0f, TFT_DARKGREY, (i & 2) != 0);
  }
}

// ---- main ---------------------------------------------------------------------------------------
void setup()
{
  lcd0 = new LGFX(LCD_W, LCD_H);
  lcd0->init();
#ifdef LGFX_SDL
  ddSDLInput::Begin(LCD_W, LCD_H);
#endif

  spr0 = new LGFX_Sprite(lcd0);
  spr0->setColorDepth(16);  // set depth first so createSprite allocates a 16-bit buffer once
  spr0->createSprite(lcd0->width(), lcd0->height());

  img = new LGFX_Sprite(); img->setColorDepth(16); img->createSprite(32, 32);
  img->fillScreen(TFT_MAGENTA);                              // magenta = transparent key
  img->fillCircle(16, 16, 15, TFT_ORANGE); img->fillCircle(16, 16, 9, TFT_YELLOW); img->fillRect(14, 2, 4, 12, TFT_RED);

  r = new ddR(spr0, -160.0f, 160.0f, -120.0f, 120.0f);       // 4:3 world fills the 4:3 screen
  interact = new ddInteract(r, &scene);
  interact->OnEvent = OnEvent;

  BuildShowcase();
  AddParticles(1500);
}

bool GetTouch(ddP *p) {
#ifdef LGFX_SDL
  float x, y;
  if (!ddSDLInput::Get(x, y)) return false;      // window-scale aware (not lcd0->getTouch())
  p->Set(x, y);
  return true;
#else
  int16_t tX, tY;
  if (lcd0->getTouch(&tX, &tY)) { (*p)[0] = tX; (*p)[1] = tY; return true; }
  return false;
#endif
}

static uint64_t NowUs() {
  using namespace std::chrono;
  return (uint64_t)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

void loop() {
  static uint64_t last = NowUs();
  static float emaFrame = 16.0f, emaDraw = 5.0f;
  static uint32_t frameCount = 0;
  uint64_t now = NowUs();
  float dt = (now - last) / 1e6f; last = now;
  if (dt > 0.05f) dt = 0.05f;
  frameCount++;
  float t = frameCount * 0.02f;

  // ---- input ----
  ddP T;
  bool down = GetTouch(&T);
  interact->Update(down, T);

  // ---- animate ----
  aRect->SetAngle(t); aHex->SetAngle(-t * 2.0f); aStar->SetAngle(t * 3.0f); aStar->SetScale(0.8f + 0.2f * sinf(t * 4.0f));
  aEllipse->SetAngle(t * 2.0f);
  aPie->EndAngle = 2.0f + 3.0f * (0.5f + 0.5f * sinf(t * 2.0f));
  aCurve->SetAngle(t); aSprite->SetAngle(-t); aSprite->SetScale(1.0f + 0.3f * sinf(t * 3.0f));
  solar->SetAngle(t * 0.5f); planetA->SetAngle(t * 2.0f); planetB->SetAngle(t * 6.0f);
  aMin->SetAngle(-t * 6.0f); aHour->SetAngle(-t * 0.5f);
  aGear->SetAngle(t);
  aGauge->EndAngle = DDD_DEG2RAD(210) - DDD_DEG2RAD(240) * (0.5f + 0.5f * sinf(t));

  // particles: move, bounce, spin; optional churn (replace some with fresh ones of the same kind)
  for (Particle &p : parts) {
    const ddT &x = p.P->GetTransform();
    float px = x.Tx + p.vx * dt, py = x.Ty + p.vy * dt;
    if (px < -FIELD_X || px > FIELD_X) { p.vx = -p.vx; px = x.Tx; }
    if (py < FIELD_YLO || py > FIELD_YHI) { p.vy = -p.vy; py = x.Ty; }
    p.P->SetPos(px, py);
    p.P->SetAngle(x.Angle + p.va * dt);
  }
  for (int i = 0, n = parts.empty() ? 0 : churnRate[churnMode]; i < n; i++) {
    size_t k = (size_t)(rnd() * parts.size()) % parts.size();
    int kind = parts[k].P->Tag - 100;
    interact->Forget(parts[k].P);
    scene.Remove(parts[k].P);
    ddVectorPrimitive *np = MakeParticle(kind);
    if (!np) { parts[k] = parts.back(); parts.pop_back(); if (parts.empty()) break; continue; }
    parts[k].P = np;
  }

  // ---- draw ----
  uint64_t d0 = NowUs();
  spr0->clear();
  r->ResetStats();
  scene.Draw(r);
  overlay.Clear();
  if (down) {
    ddP W = r->D2W * T;
    overlay.Emplace<ddMarker>(W, ddMarkerType::Plus, 10, TFT_CYAN);
    overlay.Emplace<ddCircle>(W, 4.0f, TFT_CYAN);
  }
  overlay.Draw(r);
  interact->DrawSelection(TFT_WHITE);
  float drawMs = (NowUs() - d0) / 1000.0f;

  // ---- HUD ----
  emaFrame += (dt * 1000.0f - emaFrame) * 0.05f;
  emaDraw  += (drawMs - emaDraw) * 0.05f;
  ddArena *A = scene.Arena();
  char line[96];
  spr0->fillRect(0, 0, 470, 62, TFT_BLACK);
  int fps = emaFrame > 0.01f ? (int)(1000.0f / emaFrame) : 0;
  snprintf(line, sizeof(line), "FPS %d   frame %d.%d ms   draw %d.%d ms", fps, (int)emaFrame, (int)(emaFrame * 10) % 10, (int)emaDraw, (int)(emaDraw * 10) % 10);
  r->DrawString(ddP(4, 4), line, TFT_GREEN, false);
  snprintf(line, sizeof(line), "objects %d   drawn %u   culled %u   particles %d", scene.Count(), (unsigned)r->Drawn, (unsigned)r->Culled, (int)parts.size());
  r->DrawString(ddP(4, 16), line, TFT_GREEN, false);
  snprintf(line, sizeof(line), "arena in use %d KB   peak %d KB   held %d KB / %d KB cap%s", (int)(A->InUse() / 1024), (int)(A->Peak() / 1024),
           (int)(A->Capacity() / 1024), (int)(A->Max() / 1024), A->Overflowed() ? "  OVERFLOW" : "");
  r->DrawString(ddP(4, 28), line, A->Overflowed() ? TFT_RED : TFT_GREEN, false);
  snprintf(line, sizeof(line), "culling %s   churn %d swaps/frame", r->CullEnabled ? "ON" : "OFF", churnRate[churnMode]);
  r->DrawString(ddP(4, 40), line, TFT_GREEN, false);

  spr0->pushSprite(0, 0);
  delayMs(1);
}
