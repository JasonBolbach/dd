// Unit + stress tests for the dd* headers.  Build/run:  cmake --build build --target run_tests
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include "ddVector.h"
#include "ddInteract.h"
#include "ddSDLInput.h"
#include "ddGeom.h"
#include "ddCollide.h"

static int Failures = 0, Checks = 0;
#define CHECK(c) do { Checks++; if (!(c)) { Failures++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define SECTION(n) printf("[%s]\n", n)

using Clock = std::chrono::steady_clock;
static double MsSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

struct Probe : ddVectorPrimitive {
  static int Live, Dtors;
  Probe() { Live++; }
  ~Probe() { Live--; Dtors++; }
 protected:
  void DrawShape(const Matrix3x3 &, ddR *) override {}
};
int Probe::Live = 0, Probe::Dtors = 0;

static bool Lit(LGFX_Sprite &s, int x, int y) { return s.readPixel(x, y) != 0; }
static int CountLit(LGFX_Sprite &s, int x0, int y0, int x1, int y1) {
  int n = 0;
  for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) if (Lit(s, x, y)) n++;
  return n;
}

static void TestArena() {
  SECTION("arena");
  {
    ddArena a(1024, 256);
    int n = 0;
    while (a.New<ddP>()) { if (++n > 1000) break; }
    CHECK(n < 1000);
    CHECK(a.Overflowed());
    CHECK(a.Capacity() <= 1024);
    a.Reset();
    CHECK(a.InUse() == 0);
    CHECK(a.New<ddP>() != nullptr);
  }
  {   // free list reuse + accounting
    ddArena a(64 * 1024);
    ddP *p1 = a.New<ddP>(1.0f, 2.0f);
    size_t used = a.InUse();
    a.Delete(p1);
    CHECK(a.InUse() == 0);
    ddP *p2 = a.New<ddP>();
    CHECK(p2 == p1);                       // same size class -> recycled block
    CHECK(a.InUse() == used);
    a.Delete(p2);
    a.Delete(p2);                          // double free is ignored
    CHECK(a.InUse() == 0);
  }
  {   // destructors: Delete, Reset ordering
    Probe::Live = Probe::Dtors = 0;
    ddArena a(64 * 1024);
    Probe *p = a.New<Probe>(); a.New<Probe>(); a.New<Probe>();
    CHECK(Probe::Live == 3);
    a.Delete(p);
    CHECK(Probe::Live == 2 && Probe::Dtors == 1);
    a.Reset();
    CHECK(Probe::Live == 0 && Probe::Dtors == 3);
  }
  {   // churn must not grow capacity
    ddArena a(256 * 1024);
    ddP *slots[256] = {};
    for (int i = 0; i < 256; i++) slots[i] = a.New<ddP>();
    size_t cap = a.Capacity();
    unsigned seed = 12345;
    for (int i = 0; i < 300000; i++) {
      seed = seed * 1664525u + 1013904223u;
      int k = (seed >> 8) & 255;
      a.Delete(slots[k]);
      slots[k] = (seed & 0x10000) ? a.New<ddP>() : (ddP *)a.NewArray<float>(1 + ((seed >> 20) & 7));
      if (!slots[k]) { CHECK(false); break; }
    }
    CHECK(a.Capacity() <= cap + 4 * DD_ARENA_CHUNK);
    printf("  churn: capacity %zu -> %zu bytes, peak live %zu\n", cap, a.Capacity(), a.Peak());
  }
}

static void TestScene() {
  SECTION("scene / pick / groups");
  Probe::Live = Probe::Dtors = 0;
  RenderBlock b(256 * 1024);
  ddCircle *c = b.Emplace<ddCircle>(ddP(10, 10), 5.0f, TFT_WHITE, true);
  ddRect *rc = b.Emplace<ddRect>(ddP(-20, 0), 10.0f, 6.0f);
  ddP tri[3] = { {0, 0}, {10, 0}, {0, 10} };
  ddPolygon *pg = b.EmplacePolygon(tri, 3, TFT_RED, true, true); pg->SetPos(50, 50);
  ddGroup *g = b.EmplaceGroup(); g->SetPos(0, -50); g->SetAngle(DDD_DEG2RAD(90));
  g->Children.Emplace<ddLine>(ddP(0, 0), ddP(20, 0));        // after the 90 degree turn it runs along +Y
  g->Children.Emplace<Probe>();
  b.Emplace<Probe>();
  CHECK(b.Pick(ddP(10, 10)) == c);
  CHECK(b.Pick(ddP(12, 12)) == c);
  CHECK(b.Pick(ddP(-15, 0), 0.1f) == rc);      // outline hit on the edge
  CHECK(b.Pick(ddP(-20, 0)) == nullptr);       // centre of an unfilled rect
  CHECK(b.Pick(ddP(52, 52)) == pg);
  CHECK(b.Pick(ddP(59, 59)) == nullptr);
  CHECK(b.Pick(ddP(0, -40), 0.5f) == g);
  CHECK(b.Pick(ddP(20, -50), 0.5f) == nullptr);
  c->Visible = false;
  CHECK(b.Pick(ddP(10, 10)) == nullptr);
  b.Remove(rc);
  CHECK(b.Pick(ddP(-15, 0), 0.1f) == nullptr);
  CHECK(Probe::Live == 2);
  b.Remove(g);                                  // group removal frees its children
  CHECK(Probe::Live == 1);
  b.Clear();
  CHECK(Probe::Live == 0 && b.Count() == 0);

  // reset with nested groups must destroy every object exactly once
  Probe::Live = Probe::Dtors = 0;
  for (int i = 0; i < 20; i++) {
    ddGroup *gg = b.EmplaceGroup();
    for (int k = 0; k < 5; k++) gg->Children.Emplace<Probe>();
    ddGroup *inner = gg->Children.EmplaceGroup();
    inner->Children.Emplace<Probe>();
  }
  CHECK(Probe::Live == 120);
  b.Clear();
  CHECK(Probe::Live == 0 && Probe::Dtors == 120);

  // z-order
  ddVectorPrimitive *a1 = b.Emplace<ddCircle>(ddP(0, 0), 5.0f, TFT_WHITE, true);
  ddVectorPrimitive *a2 = b.Emplace<ddCircle>(ddP(0, 0), 5.0f, TFT_WHITE, true);
  CHECK(b.Pick(ddP(0, 0)) == a2);
  b.MoveToFront(a1); CHECK(b.Pick(ddP(0, 0)) == a1);
  b.MoveToBack(a1);  CHECK(b.Pick(ddP(0, 0)) == a2);
  b.Clear();
}

static void TestRender() {
  SECTION("render / culling / pens / sprite");
  LGFX_Sprite spr; spr.setColorDepth(16); spr.createSprite(320, 240);
  ddR r(&spr, -160.0f, 160.0f, -120.0f, 120.0f);        // 1 unit = 1 px
  CHECK(fabsf(r.PixelsPerUnit() - 1.0f) < 1e-4f);
  CHECK(fabsf(r.ViewXMin + 160.0f) < 1e-3f && fabsf(r.ViewYMax - 120.0f) < 1e-3f);
  RenderBlock b(256 * 1024);

  auto draw = [&]() { spr.clear(); r.ResetStats(); b.Draw(&r); };

  // filled shapes cover their centre
  auto *fr = b.Emplace<ddRect>(ddP(-100, 50), 20.0f, 10.0f, TFT_WHITE, true);
  ddP star[10];
  for (int i = 0; i < 10; i++) { float a = DDD_DEG2RAD(90.0f + i * 36.0f), rad = (i & 1) ? 8.0f : 20.0f; star[i].Set(rad * cosf(a), rad * sinf(a)); }
  auto *st = b.EmplacePolygon(star, 10, TFT_WHITE, true, true); st->SetPos(-50, 50);
  auto *fc = b.Emplace<ddCircle>(ddP(0, 50), 10.0f, TFT_WHITE, true);
  auto *hx = b.Emplace<ddNGon>(ddP(50, 50), 6, 12.0f, TFT_WHITE, true);
  draw();
  CHECK(Lit(spr, 160 - 100, 120 - 50));
  CHECK(Lit(spr, 160 - 50, 120 - 50));          // concave star centre filled
  CHECK(Lit(spr, 160, 120 - 50));
  CHECK(Lit(spr, 160 + 50, 120 - 50));
  CHECK(!Lit(spr, 160 - 50 + 19, 120 - 50 - 6)); // between star arms stays empty (concave)
  CHECK(r.Drawn == 4 && r.Culled == 0);
  b.Clear();

  // transforms: rotated rectangle uses the polygon path and stays filled
  auto *rr = b.Emplace<ddRect>(ddP(0, 0), 40.0f, 10.0f, TFT_WHITE, true);
  rr->SetAngle(DDD_DEG2RAD(90));
  draw();
  CHECK(Lit(spr, 160, 120 - 15) && !Lit(spr, 160 + 15, 120));   // now tall, not wide
  b.Clear();

  // culling
  auto *far1 = b.Emplace<ddCircle>(ddP(1000, 0), 5.0f, TFT_WHITE, true);
  auto *near1 = b.Emplace<ddCircle>(ddP(0, 0), 5.0f, TFT_WHITE, true);
  auto *edge = b.Emplace<ddCircle>(ddP(163, 0), 5.0f, TFT_WHITE, true);     // straddles the right edge
  auto *grp = b.EmplaceGroup(); grp->SetPos(-1000, 0);
  grp->Children.Emplace<ddCircle>(ddP(0, 0), 5.0f);
  grp->Children.Emplace<ddLine>(ddP(0, 0), ddP(10, 10));
  draw();
  CHECK(r.Culled == 2 && r.Drawn == 2);                    // far1 and the whole group
  CHECK(Lit(spr, 160, 120) && Lit(spr, 318, 120));
  r.CullEnabled = false;
  draw();
  CHECK(r.Culled == 0 && r.Drawn == 6);                       // far1, near1, edge, group + its 2 children
  r.CullEnabled = true;
  (void)far1; (void)near1; (void)edge;
  b.Clear();

  // pens: wide line is thicker, dashed has gaps
  auto *thin = b.Emplace<ddLine>(ddP(-100, 0), ddP(100, 0), TFT_WHITE);
  auto *wide = b.Emplace<ddLine>(ddP(-100, 20), ddP(100, 20), TFT_WHITE); wide->Pen.Width = 5;
  auto *dash = b.Emplace<ddLine>(ddP(-100, 40), ddP(100, 40), TFT_WHITE); dash->Pen.Style = ddLineStyle::Dashed; dash->Pen.Dash = 8; dash->Pen.Gap = 8;
  auto *dot  = b.Emplace<ddLine>(ddP(-100, 60), ddP(100, 60), TFT_WHITE); dot->Pen.Style = ddLineStyle::Dotted;
  draw();
  int nThin = CountLit(spr, 60, 120 - 2, 260, 120 + 2);
  int nWide = CountLit(spr, 60, 120 - 20 - 4, 260, 120 - 20 + 4);
  int nDash = CountLit(spr, 60, 120 - 40 - 2, 260, 120 - 40 + 2);
  int nDot  = CountLit(spr, 60, 120 - 60 - 2, 260, 120 - 60 + 2);
  printf("  pixels: thin %d wide %d dash %d dot %d\n", nThin, nWide, nDash, nDot);
  CHECK(nThin >= 195 && nThin <= 205);
  CHECK(nWide > 3 * nThin);
  CHECK(nDash > 80 && nDash < nThin - 40);
  CHECK(nDot > 20 && nDot < nDash);
  (void)thin;
  b.Clear();

  // wide pen on a circle forces the polyline path (still drawn, thicker than plain)
  auto *pc = b.Emplace<ddCircle>(ddP(0, 0), 30.0f); draw(); int plain = CountLit(spr, 100, 60, 220, 180);
  pc->Pen.Width = 4; draw(); int thick = CountLit(spr, 100, 60, 220, 180);
  CHECK(thick > plain * 2);
  b.Clear();

  // sprite primitive
  LGFX_Sprite img; img.setColorDepth(16); img.createSprite(16, 16); img.fillScreen(TFT_RED);
  auto *sp = b.Emplace<ddSprite>(ddP(0, 0), &img, 1.0f);
  draw();
  CHECK(Lit(spr, 160, 120) && Lit(spr, 153, 113) && !Lit(spr, 160 + 12, 120));
  sp->SetAngle(DDD_DEG2RAD(45)); sp->SetScale(2.0f);
  draw();
  CHECK(Lit(spr, 160, 120) && Lit(spr, 160 + 20, 120) && !Lit(spr, 160 + 24, 120 + 8 ));
  CHECK(b.Pick(ddP(5, 5)) == sp);
  b.Clear();

  // arcs, beziers, arrows, markers, grid: just must draw something and survive transforms
  b.Emplace<ddArc>(ddP(0, 0), 30.0f, 0.0f, 3.0f, TFT_WHITE, true, true);
  b.Emplace<ddBezier>(ddP(-50, -50), ddP(0, 50), ddP(50, -50), TFT_WHITE);
  b.Emplace<ddArrow>(ddP(-30, -30), ddP(30, 30), TFT_WHITE);
  b.Emplace<ddMarker>(ddP(80, 80), ddMarkerType::Diamond, 5, TFT_WHITE, true);
  b.Emplace<ddGrid>(20.0f, 100.0f, 100.0f);
  draw();
  CHECK(CountLit(spr, 0, 0, 319, 239) > 500);
  b.Clear();
}

struct EventLog { int press = 0, drag = 0, release = 0, tap = 0; ddVectorPrimitive *last = nullptr; };
static void OnEv(ddEvent e, ddVectorPrimitive *p, const ddP &, void *u) {
  EventLog *l = (EventLog *)u;
  l->last = p;
  switch (e) { case ddEvent::Press: l->press++; break; case ddEvent::Drag: l->drag++; break;
               case ddEvent::Release: l->release++; break; case ddEvent::Tap: l->tap++; break; }
}

static void TestInteract() {
  SECTION("interaction");
  LGFX_Sprite spr; spr.setColorDepth(16); spr.createSprite(320, 240);
  ddR r(&spr, -160.0f, 160.0f, -120.0f, 120.0f);
  RenderBlock b(64 * 1024);
  ddCircle *c = b.Emplace<ddCircle>(ddP(0, 0), 10.0f, TFT_WHITE, true);
  c->Draggable = true;
  ddCircle *fixed = b.Emplace<ddCircle>(ddP(60, 0), 10.0f, TFT_WHITE, true);
  ddInteract in(&r, &b);
  EventLog log; in.OnEvent = OnEv; in.User = &log;

  // tap on the draggable circle (display centre = world origin)
  in.Update(true, ddP(160, 120)); in.Update(false, ddP(160, 120));
  CHECK(log.press == 1 && log.release == 1 && log.tap == 1 && in.Selected == c);
  // drag by (+30, -20) display px -> world (+30, +20)
  in.Update(true, ddP(160, 120));
  for (int i = 1; i <= 5; i++) in.Update(true, ddP(160.0f + 6 * i, 120.0f - 4 * i));
  in.Update(false, ddP(190, 100));
  CHECK(log.drag >= 4 && log.tap == 1);
  CHECK(b.Pick(ddP(30, 20)) == c);
  // non-draggable does not move
  in.Update(true, ddP(220, 120));
  in.Update(true, ddP(240, 120)); in.Update(false, ddP(240, 120));
  CHECK(b.Pick(ddP(60, 0)) == fixed && in.Selected == fixed);
  // tap on empty space deselects
  in.Update(true, ddP(10, 10)); in.Update(false, ddP(10, 10));
  CHECK(in.Selected == nullptr);
}

static void TestSDLInput() {
  SECTION("sdl input scaling");
  int lx, ly;
  // window exactly the frame size, 1x display: identity
  ddSDLInput::Scale(100, 50, 1280, 960, 1280, 960, 1280, 960, lx, ly); CHECK(lx == 100 && ly == 50);
  // hi-dpi: renderer output is 2x the window in points; points still map 1:1 to logical pixels
  ddSDLInput::Scale(640, 480, 1280, 960, 2560, 1920, 1280, 960, lx, ly); CHECK(lx == 640 && ly == 480);
  // wider window (1600x960): image stays 1:1 and is centred with 160 px bars each side
  ddSDLInput::Scale(160, 0, 1600, 960, 1600, 960, 1280, 960, lx, ly); CHECK(lx == 0 && ly == 0);
  ddSDLInput::Scale(800, 480, 1600, 960, 1600, 960, 1280, 960, lx, ly); CHECK(lx == 640 && ly == 480);
  ddSDLInput::Scale(0, 480, 1600, 960, 1600, 960, 1280, 960, lx, ly); CHECK(lx < 0);          // in the bar
  ddSDLInput::Scale(1599, 480, 1600, 960, 1600, 960, 1280, 960, lx, ly); CHECK(lx >= 1280);   // in the bar
  // window twice the frame: scale 2.0
  ddSDLInput::Scale(1280, 960, 2560, 1920, 2560, 1920, 1280, 960, lx, ly); CHECK(lx == 640 && ly == 480);
  // aspect lock: width drives when it moved most, otherwise height
  int nw, nh;
  ddSDLInput::AspectFit(1600, 900, 1280, 960, 1280, 960, nw, nh); CHECK(nw == 1600 && nh == 1200);
  ddSDLInput::AspectFit(1280, 600, 1280, 960, 1280, 960, nw, nh); CHECK(nh == 600 && nw == 800);
  ddSDLInput::AspectFit(1280, 960, 1280, 960, 1280, 960, nw, nh); CHECK(nw == 1280 && nh == 960);
}


static bool Near(float a, float b, float eps = 1e-3f) { return fabsf(a - b) <= eps; }
static bool NearP(const ddP &p, float x, float y, float eps = 1e-3f) { return Near(p[0], x, eps) && Near(p[1], y, eps); }

static void TestGeom() {
  SECTION("geometry: point / line / circle");
  const ddP S(-777.0f, -777.0f);                       // sentinel: must stay untouched when there is no contact
  ddP q1 = S, q2 = S;

  // ---- point vs line
  CHECK(ddCollidePointLine(ddP(5, 0), ddP(0, 0), ddP(10, 0)) == 1);
  CHECK(ddCollidePointLine(ddP(5, 0.5f), ddP(0, 0), ddP(10, 0)) == 0);
  CHECK(ddCollidePointLine(ddP(5, 0.5f), ddP(0, 0), ddP(10, 0), 1.0f) == 1);          // tolerance
  CHECK(ddCollidePointLine(ddP(12, 0), ddP(0, 0), ddP(10, 0)) == 0);                  // past the segment end
  CHECK(ddCollidePointLine(ddP(12, 0), ddP(0, 0), ddP(10, 0), 1e-4f, ddExtent::Ray) == 1);
  CHECK(ddCollidePointLine(ddP(-2, 0), ddP(0, 0), ddP(10, 0), 1e-4f, ddExtent::Ray) == 0);
  CHECK(ddCollidePointLine(ddP(-2, 0), ddP(0, 0), ddP(10, 0), 1e-4f, ddExtent::Line) == 1);
  { ddP cl; float d = ddDistPointLine(ddP(4, 3), ddP(0, 0), ddP(10, 0), ddExtent::Segment, &cl);
    CHECK(Near(d, 3.0f) && NearP(cl, 4, 0)); }
  { ddP cl; float d = ddDistPointLine(ddP(15, 4), ddP(0, 0), ddP(10, 0), ddExtent::Segment, &cl);   // clamps to the end
    CHECK(Near(d, hypotf(5, 4)) && NearP(cl, 10, 0)); }
  CHECK(Near(ddDistPointLine(ddP(3, 3), ddP(5, 5), ddP(5, 5)), hypotf(2, 2)));                       // zero-length segment
  CHECK(ddSide(ddP(0, 1), ddP(0, 0), ddP(10, 0)) == 1 && ddSide(ddP(0, -1), ddP(0, 0), ddP(10, 0)) == -1 && ddSide(ddP(5, 0), ddP(0, 0), ddP(10, 0)) == 0);

  // ---- point vs circle
  CHECK(ddCollidePointCircle(ddP(1, 1), ddP(0, 0), 5.0f) == 1);
  CHECK(ddCollidePointCircle(ddP(5, 0), ddP(0, 0), 5.0f) == 1);                       // on the rim counts as inside
  CHECK(ddCollidePointCircle(ddP(6, 0), ddP(0, 0), 5.0f) == 0);
  CHECK(ddCollidePointCircle(ddP(1, 1), ddP(0, 0), 5.0f, true) == 0);                // outline only
  CHECK(ddCollidePointCircle(ddP(0, 5), ddP(0, 0), 5.0f, true) == 1);
  CHECK(Near(ddSignedDistPointCircle(ddP(8, 0), ddP(0, 0), 5.0f), 3.0f) && Near(ddSignedDistPointCircle(ddP(2, 0), ddP(0, 0), 5.0f), -3.0f));

  // ---- line vs circle
  SECTION("geometry: line / circle");
  q1 = q2 = S;
  CHECK(ddCollideLineCircle(ddP(-10, 0), ddP(10, 0), ddP(0, 0), 5.0f, &q1, &q2) == 2);
  CHECK(NearP(q1, -5, 0) && NearP(q2, 5, 0));                                         // ordered along A->B
  q1 = q2 = S;
  CHECK(ddCollideLineCircle(ddP(10, 0), ddP(-10, 0), ddP(0, 0), 5.0f, &q1, &q2) == 2);
  CHECK(NearP(q1, 5, 0) && NearP(q2, -5, 0));                                         // reversed direction -> reversed order
  q1 = q2 = S;
  CHECK(ddCollideLineCircle(ddP(-10, 5), ddP(10, 5), ddP(0, 0), 5.0f, &q1, &q2) == 1 && NearP(q1, 0, 5) && q2[0] == S[0]);   // tangent
  q1 = q2 = S;
  CHECK(ddCollideLineCircle(ddP(-10, 6), ddP(10, 6), ddP(0, 0), 5.0f, &q1, &q2) == 0 && q1[0] == S[0] && q2[0] == S[0]);      // miss leaves outputs alone
  q1 = q2 = S;
  CHECK(ddCollideLineCircle(ddP(-1, 0), ddP(1, 0), ddP(0, 0), 5.0f, &q1, &q2) == 0 && q1[0] == S[0]);                        // chord fully inside the disc
  q1 = q2 = S;
  CHECK(ddCollideLineCircle(ddP(0, 0), ddP(10, 0), ddP(0, 0), 5.0f, &q1, &q2) == 1 && NearP(q1, 5, 0));                      // starts inside, leaves once
  q1 = q2 = S;
  CHECK(ddCollideLineCircle(ddP(-10, 0), ddP(-6, 0), ddP(0, 0), 5.0f, &q1, &q2) == 0);                                       // segment stops short
  CHECK(ddCollideLineCircle(ddP(-10, 0), ddP(-6, 0), ddP(0, 0), 5.0f, &q1, &q2, ddExtent::Ray) == 2);                        // ray keeps going
  CHECK(ddCollideLineCircle(ddP(-10, 0), ddP(-12, 0), ddP(0, 0), 5.0f, &q1, &q2, ddExtent::Ray) == 0);                       // ray pointing away
  CHECK(ddCollideLineCircle(ddP(-10, 0), ddP(-12, 0), ddP(0, 0), 5.0f, &q1, &q2, ddExtent::Line) == 2);
  CHECK(ddCollideLineCircle(ddP(-10, 0), ddP(10, 0), ddP(0, 0), 5.0f) == 2);                                                   // null outputs are fine
  CHECK(ddCollideLineCircle(ddP(-10, 0), ddP(10, 0), ddP(0, 0), 5.0f, nullptr, &q2) == 2);
  q1 = S; CHECK(ddCollideLineCircle(ddP(5, 0), ddP(5, 0), ddP(0, 0), 5.0f, &q1) == 1 && NearP(q1, 5, 0));                    // zero-length on the rim
  CHECK(ddCollideLineCircle(ddP(4, 0), ddP(4, 0), ddP(0, 0), 5.0f) == 0);
  q1 = S; CHECK(ddCollideLineCircle(ddP(-3, 4), ddP(3, 4), ddP(0, 0), 0.0f, &q1) == 0);                                       // r = 0 acts as a point
  CHECK(ddCollideLineCircle(ddP(-3, 0), ddP(3, 0), ddP(0, 0), 0.0f, &q1) == 1);
  CHECK(ddCollideLineCircle(ddP(-3, 0), ddP(3, 0), ddP(0, 0), -1.0f) == 0);
  CHECK(Near(ddGapLineCircle(ddP(-10, 8), ddP(10, 8), ddP(0, 0), 5.0f), 3.0f) && ddGapLineCircle(ddP(-10, 2), ddP(10, 2), ddP(0, 0), 5.0f) < 0);

  // ---- line vs line
  SECTION("geometry: line / line");
  q1 = q2 = S;
  CHECK(ddCollideLineLine(ddP(0, 0), ddP(10, 10), ddP(0, 10), ddP(10, 0), &q1, &q2) == 1 && NearP(q1, 5, 5) && q2[0] == S[0]);
  q1 = q2 = S;
  CHECK(ddCollideLineLine(ddP(0, 0), ddP(10, 0), ddP(0, 1), ddP(10, 1), &q1, &q2) == 0 && q1[0] == S[0]);                    // parallel, apart
  CHECK(ddCollideLineLine(ddP(0, 0), ddP(4, 0), ddP(5, -3), ddP(5, 3), &q1) == 0);                                            // lines cross beyond the segment
  q1 = S; CHECK(ddCollideLineLine(ddP(0, 0), ddP(4, 0), ddP(5, -3), ddP(5, 3), &q1, nullptr, ddExtent::Line, ddExtent::Segment) == 1 && NearP(q1, 5, 0));
  q1 = S; CHECK(ddCollideLineLine(ddP(0, 0), ddP(4, 0), ddP(5, -3), ddP(5, 3), &q1, nullptr, ddExtent::Ray, ddExtent::Line) == 1 && NearP(q1, 5, 0));
  CHECK(ddCollideLineLine(ddP(4, 0), ddP(0, 0), ddP(5, -3), ddP(5, 3), &q1, nullptr, ddExtent::Ray, ddExtent::Line) == 0);   // ray pointing away
  q1 = S; CHECK(ddCollideLineLine(ddP(0, 0), ddP(5, 0), ddP(5, 0), ddP(5, 5), &q1) == 1 && NearP(q1, 5, 0));                  // touching endpoints
  q1 = S; CHECK(ddCollideLineLine(ddP(0, 0), ddP(10, 0), ddP(5, 0), ddP(5, 5), &q1) == 1 && NearP(q1, 5, 0));                 // T junction
  q1 = q2 = S;
  CHECK(ddCollideLineLine(ddP(0, 0), ddP(10, 0), ddP(4, 0), ddP(14, 0), &q1, &q2) == 2 && NearP(q1, 4, 0) && NearP(q2, 10, 0)); // collinear overlap
  q1 = q2 = S;
  CHECK(ddCollideLineLine(ddP(0, 0), ddP(10, 0), ddP(14, 0), ddP(4, 0), &q1, &q2) == 2 && NearP(q1, 4, 0) && NearP(q2, 10, 0)); // opposite direction
  q1 = q2 = S;
  CHECK(ddCollideLineLine(ddP(0, 0), ddP(10, 0), ddP(2, 0), ddP(6, 0), &q1, &q2) == 2 && NearP(q1, 2, 0) && NearP(q2, 6, 0));   // one inside the other
  q1 = q2 = S;
  CHECK(ddCollideLineLine(ddP(0, 0), ddP(10, 0), ddP(10, 0), ddP(20, 0), &q1, &q2) == 1 && NearP(q1, 10, 0) && q2[0] == S[0]); // collinear, touching at one point
  q1 = q2 = S;
  CHECK(ddCollideLineLine(ddP(0, 0), ddP(10, 0), ddP(11, 0), ddP(20, 0), &q1, &q2) == 0 && q1[0] == S[0]);                    // collinear, disjoint
  CHECK(ddCollideLineLine(ddP(0, 0), ddP(10, 0), ddP(0, 0), ddP(5, 0), nullptr, nullptr, ddExtent::Line, ddExtent::Line) == DD_INFINITE);
  CHECK(ddCollideLineLine(ddP(0, 0), ddP(10, 0), ddP(20, 0), ddP(30, 0), nullptr, nullptr, ddExtent::Ray, ddExtent::Ray) == DD_INFINITE);   // rays the same way
  q1 = q2 = S;
  CHECK(ddCollideLineLine(ddP(0, 0), ddP(10, 0), ddP(30, 0), ddP(20, 0), &q1, &q2, ddExtent::Ray, ddExtent::Ray) == 2 && NearP(q1, 0, 0) && NearP(q2, 30, 0));  // rays facing each other share [0,30]
  q1 = q2 = S;
  CHECK(ddCollideLineLine(ddP(0, 0), ddP(10, 0), ddP(30, 0), ddP(20, 0), &q1, &q2, ddExtent::Segment, ddExtent::Ray) == 2 && NearP(q1, 0, 0) && NearP(q2, 10, 0));  // ray covers the whole segment
  q1 = q2 = S;
  CHECK(ddCollideLineLine(ddP(0, 0), ddP(10, 0), ddP(30, 0), ddP(40, 0), &q1, &q2, ddExtent::Segment, ddExtent::Ray) == 0 && q1[0] == S[0]);   // ray pointing away
  q1 = S; CHECK(ddCollideLineLine(ddP(3, 3), ddP(3, 3), ddP(0, 0), ddP(10, 10), &q1) == 1 && NearP(q1, 3, 3));                // zero-length vs segment
  CHECK(ddCollideLineLine(ddP(3, 4), ddP(3, 4), ddP(0, 0), ddP(10, 10)) == 0);
  CHECK(ddCollideLineLine(ddP(1, 1), ddP(1, 1), ddP(1, 1), ddP(1, 1)) == 1);
  CHECK(Near(ddDistSegSeg(ddP(0, 0), ddP(10, 0), ddP(0, 3), ddP(10, 3)), 3.0f));
  CHECK(Near(ddDistSegSeg(ddP(0, 0), ddP(10, 0), ddP(5, -1), ddP(5, 1)), 0.0f));
  { ddP a, b; float d = ddDistSegSeg(ddP(0, 0), ddP(10, 0), ddP(13, 4), ddP(20, 4), &a, &b);
    CHECK(Near(d, 5.0f) && NearP(a, 10, 0) && NearP(b, 13, 4)); }
  CHECK(Near(ddAngle(ddP(0, 0), ddP(1, 0), ddP(0, 0), ddP(0, 1)), DDD_PI * 0.5f) && Near(ddSignedAngle(ddP(0, 0), ddP(1, 0), ddP(0, 0), ddP(0, -1)), -DDD_PI * 0.5f));

  // ---- circle vs circle
  SECTION("geometry: circle / circle");
  q1 = q2 = S;
  CHECK(ddCollideCircleCircle(ddP(0, 0), 5.0f, ddP(6, 0), 5.0f, &q1, &q2) == 2 && NearP(q1, 3, 4) && NearP(q2, 3, -4));      // p1 left of c1->c2
  q1 = q2 = S;
  CHECK(ddCollideCircleCircle(ddP(0, 0), 5.0f, ddP(10, 0), 5.0f, &q1, &q2) == 1 && NearP(q1, 5, 0) && q2[0] == S[0]);        // external tangent
  q1 = q2 = S;
  CHECK(ddCollideCircleCircle(ddP(0, 0), 5.0f, ddP(2, 0), 3.0f, &q1, &q2) == 1 && NearP(q1, 5, 0));                          // internal tangent
  q1 = q2 = S;
  CHECK(ddCollideCircleCircle(ddP(0, 0), 5.0f, ddP(11, 0), 5.0f, &q1, &q2) == 0 && q1[0] == S[0]);                           // apart
  CHECK(ddCollideCircleCircle(ddP(0, 0), 5.0f, ddP(1, 0), 2.0f) == 0);                                                        // nested
  CHECK(ddCollideCircleCircle(ddP(0, 0), 5.0f, ddP(0, 0), 2.0f) == 0);                                                        // concentric
  CHECK(ddCollideCircleCircle(ddP(0, 0), 5.0f, ddP(0, 0), 5.0f) == DD_INFINITE);
  CHECK(Near(ddGapCircleCircle(ddP(0, 0), 5.0f, ddP(12, 0), 5.0f), 2.0f) && ddGapCircleCircle(ddP(0, 0), 5.0f, ddP(8, 0), 5.0f) < 0);
  CHECK(ddPointInPolygon(ddP(1, 1), (const ddP[]){ ddP(0, 0), ddP(4, 0), ddP(4, 4), ddP(0, 4) }, 4));

  // ---- randomised properties: every reported contact really lies on both shapes
  SECTION("geometry: randomised properties");
  unsigned seed = 2024;
  auto rnd = [&](float lo, float hi) { seed = seed * 1664525u + 1013904223u; return lo + (hi - lo) * ((seed >> 8) / 16777216.0f); };
  int lcHits = 0, llHits = 0, ccHits = 0, bad = 0;
  for (int i = 0; i < 20000; i++) {
    float off = (i % 3 == 0) ? 500.0f : 0.0f;                      // also exercise coordinates far from the origin
    ddP a(rnd(-20, 20) + off, rnd(-20, 20)), b(rnd(-20, 20) + off, rnd(-20, 20));
    ddP c(rnd(-15, 15) + off, rnd(-15, 15)); float r = rnd(0.5f, 15.0f);
    ddExtent ext = (ddExtent)(i % 3);
    ddP h1, h2;
    int n = ddCollideLineCircle(a, b, c, r, &h1, &h2, ext);
    float tolp = 2e-3f * (1.0f + off * 0.01f);
    if (n >= 1) { lcHits++; if (fabsf(h1.DistanceTo(c) - r) > tolp || ddDistPointLine(h1, a, b, ext) > tolp) bad++; }
    if (n == 2) { if (fabsf(h2.DistanceTo(c) - r) > tolp || ddDistPointLine(h2, a, b, ext) > tolp) bad++; }
    // brute force: sample the line and count rim crossings, compare with n for the segment case
    if (ext == ddExtent::Segment) {
      int cross = 0; bool prevIn = a.DistanceTo(c) < r;
      for (int k = 1; k <= 400; k++) { bool in = ddAlong(a, b - a, k / 400.0f).DistanceTo(c) < r; if (in != prevIn) cross++; prevIn = in; }
      if (cross != n && !(cross == 0 && n == 1) && !(cross == 0 && n == 2)) bad++;   // sampling can miss grazing / tangent contacts, never invent them
    }
    ddP cc(rnd(-20, 20) + off, rnd(-20, 20)), dd(rnd(-20, 20) + off, rnd(-20, 20));
    ddExtent e2 = (ddExtent)((i / 3) % 3);
    int m = ddCollideLineLine(a, b, cc, dd, &h1, &h2, ext, e2);
    if (m == 1) { llHits++; if (ddDistPointLine(h1, a, b, ext) > tolp || ddDistPointLine(h1, cc, dd, e2) > tolp) bad++; }
    if (m == 2) { if (ddDistPointLine(h1, a, b, ext) > tolp || ddDistPointLine(h2, cc, dd, e2) > tolp) bad++; }
    // symmetry: swapping the two lines gives the same count
    if (ddCollideLineLine(cc, dd, a, b, nullptr, nullptr, e2, ext) != m) bad++;
    ddP c2(rnd(-15, 15) + off, rnd(-15, 15)); float r2 = rnd(0.5f, 15.0f);
    int k = ddCollideCircleCircle(c, r, c2, r2, &h1, &h2);
    if (k >= 1) { ccHits++; if (fabsf(h1.DistanceTo(c) - r) > tolp || fabsf(h1.DistanceTo(c2) - r2) > tolp) bad++; }
    if (k == 2) { if (fabsf(h2.DistanceTo(c) - r) > tolp || fabsf(h2.DistanceTo(c2) - r2) > tolp) bad++; }
  }
  printf("  random: %d line/circle, %d line/line, %d circle/circle contacts verified, %d violations\n", lcHits, llHits, ccHits, bad);
  CHECK(bad == 0 && lcHits > 1000 && llHits > 1000 && ccHits > 1000);
}


static void TestBox() {
  SECTION("geometry: axis-aligned box");
  const ddP S(-777.0f, -777.0f);
  ddP q1 = S, q2 = S;
  ddBox e; CHECK(e.Empty() && e.Width() == 0.0f);
  ddBox b(10, 5, 0, 0);                                       // corners given in any order
  CHECK(!b.Empty() && Near(b.Width(), 10) && Near(b.Height(), 5) && NearP(b.Center(), 5, 2.5f));
  e.Add(ddP(3, 4)); e.Add(ddP(-1, 9)); CHECK(Near(e.X0, -1) && Near(e.X1, 3) && Near(e.Y0, 4) && Near(e.Y1, 9));
  CHECK(NearP(b.Corner(0), 0, 0) && NearP(b.Corner(1), 10, 0) && NearP(b.Corner(2), 10, 5) && NearP(b.Corner(3), 0, 5));
  CHECK(b.Grown(1).Contains(ddP(-1, -1)) && !b.Contains(ddP(-1, -1)));

  CHECK(ddCollidePointBox(ddP(5, 2), b) == 1 && ddCollidePointBox(ddP(10, 5), b) == 1 && ddCollidePointBox(ddP(10.5f, 2), b) == 0);
  CHECK(Near(ddDistPointBox(ddP(13, 9), b), 5.0f) && Near(ddDistPointBox(ddP(5, 2), b), 0.0f));
  CHECK(NearP(ddClosestPointOnBox(ddP(13, 9), b), 10, 5) && NearP(ddClosestPointOnBox(ddP(5, 2), b), 5, 2));

  ddBox ov;
  CHECK(ddCollideBoxBox(b, ddBox(8, 3, 20, 20), &ov) == 1 && Near(ov.X0, 8) && Near(ov.Y0, 3) && Near(ov.X1, 10) && Near(ov.Y1, 5));
  CHECK(ddCollideBoxBox(b, ddBox(10, 0, 20, 5)) == 1);                                  // sharing an edge touches
  CHECK(ddCollideBoxBox(b, ddBox(10.5f, 0, 20, 5)) == 0);
  CHECK(ddCollideBoxBox(b, ddBox(2, 1, 3, 2), &ov) == 1 && Near(ov.X0, 2) && Near(ov.X1, 3));      // nested
  CHECK(ddCollideBoxBox(b, ddBox()) == 0);                                                          // empty never collides

  // line vs box outline
  q1 = q2 = S;
  CHECK(ddCollideLineBox(ddP(-5, 2), ddP(15, 2), b, &q1, &q2) == 2 && NearP(q1, 0, 2) && NearP(q2, 10, 2));
  q1 = q2 = S;
  CHECK(ddCollideLineBox(ddP(15, 2), ddP(-5, 2), b, &q1, &q2) == 2 && NearP(q1, 10, 2) && NearP(q2, 0, 2));   // order follows A->B
  q1 = q2 = S;
  CHECK(ddCollideLineBox(ddP(5, 2), ddP(15, 2), b, &q1, &q2) == 1 && NearP(q1, 10, 2) && q2[0] == S[0]);     // starts inside
  q1 = q2 = S;
  CHECK(ddCollideLineBox(ddP(2, 2), ddP(8, 3), b, &q1, &q2) == 0 && q1[0] == S[0]);                          // fully inside: no outline contact
  CHECK(ddCollideLineBox(ddP(-5, 8), ddP(15, 8), b, &q1, &q2) == 0);                                          // misses
  CHECK(ddCollideLineBox(ddP(-5, 2), ddP(-1, 2), b) == 0);                                                    // stops short
  CHECK(ddCollideLineBox(ddP(-5, 2), ddP(-1, 2), b, nullptr, nullptr, ddExtent::Ray) == 2);                   // a ray carries on
  CHECK(ddCollideLineBox(ddP(-5, 2), ddP(-9, 2), b, nullptr, nullptr, ddExtent::Ray) == 0);
  CHECK(ddCollideLineBox(ddP(-5, 2), ddP(-9, 2), b, nullptr, nullptr, ddExtent::Line) == 2);
  q1 = q2 = S;
  CHECK(ddCollideLineBox(ddP(-5, 0), ddP(15, 0), b, &q1, &q2) == 2 && NearP(q1, 0, 0) && NearP(q2, 10, 0));   // along an edge
  q1 = q2 = S;
  CHECK(ddCollideLineBox(ddP(-5, -5), ddP(0, 0), b, &q1, &q2) == 1 && NearP(q1, 0, 0));                       // touches a corner
  CHECK(ddCollideLineBox(ddP(-5, 7), ddP(7, -5), b, &q1, &q2) == 2);                                          // diagonal through a corner region
  q1 = S; CHECK(ddCollideLineBox(ddP(10, 3), ddP(10, 3), b, &q1) == 1 && NearP(q1, 10, 3));                  // zero-length on the outline
  CHECK(ddCollideLineBox(ddP(5, 3), ddP(5, 3), b) == 0);

  // circle vs box
  ddP cl;
  CHECK(ddCollideCircleBox(ddP(5, 2), 1.0f, b, &cl) == 1 && NearP(cl, 5, 2));                                 // centre inside
  CHECK(ddCollideCircleBox(ddP(12, 2), 2.5f, b, &cl) == 1 && NearP(cl, 10, 2));
  CHECK(ddCollideCircleBox(ddP(12, 2), 1.5f, b) == 0);
  CHECK(ddCollideCircleBox(ddP(13, 9), 5.0f, b) == 1 && ddCollideCircleBox(ddP(13, 9), 4.9f, b) == 0);          // corner distance
  { ddP pts[8];
    CHECK(ddCollideCircleBoxOutline(ddP(5, 2.5f), 3.0f, b, pts, 8) == 4);                                      // crosses the top and bottom edges twice each (r=3 reaches y -0.5 .. 5.5)
    CHECK(ddCollideCircleBoxOutline(ddP(5, 2.5f), 4.0f, ddBox(0, 0, 10, 5), pts, 8) == 4);
    CHECK(ddCollideCircleBoxOutline(ddP(5, 2.5f), 1.0f, b, pts, 8) == 0);                                        // inside
    CHECK(ddCollideCircleBoxOutline(ddP(5, 5), 6.0f, ddBox(0, 0, 10, 10), pts, 8) == 8);                        // crosses every edge twice
    CHECK(ddCollideCircleBoxOutline(ddP(5, 5), 5.0f, ddBox(0, 0, 10, 10), pts, 8) == 4);                       // tangent to each edge midpoint
    CHECK(ddCollideCircleBoxOutline(ddP(5, 5), 7.2f, ddBox(0, 0, 10, 10), pts, 8) == 0);                       // r > corner distance: the whole box is inside the circle
    CHECK(ddCollideCircleBoxOutline(ddP(5, 5), 20.0f, ddBox(0, 0, 10, 10), pts, 8) == 0); }                    // box inside the circle
  CHECK(Near(ddBoxOfCircle(ddP(2, 3), 4).X0, -2) && Near(ddBoxOfCircle(ddP(2, 3), 4).Y1, 7));
}

// separating-axis reference for two convex quads (independent of the library code)
static bool SatOverlap(const ddP *A, const ddP *B) {
  for (int pass = 0; pass < 2; pass++) {
    const ddP *P = pass ? B : A;
    for (int i = 0; i < 4; i++) {
      float ex = P[(i + 1) & 3][0] - P[i][0], ey = P[(i + 1) & 3][1] - P[i][1];
      float nx = -ey, ny = ex;
      float amin = 1e30f, amax = -1e30f, bmin = 1e30f, bmax = -1e30f;
      for (int k = 0; k < 4; k++) {
        float da = A[k][0] * nx + A[k][1] * ny, db = B[k][0] * nx + B[k][1] * ny;
        amin = fminf(amin, da); amax = fmaxf(amax, da); bmin = fminf(bmin, db); bmax = fmaxf(bmax, db);
      }
      if (amax < bmin || bmax < amin) return false;
    }
  }
  return true;
}
static void RectCorners(float cx, float cy, float w, float h, float ang, ddP *out) {
  float c = cosf(ang), s = sinf(ang), hx[4] = { -w / 2, w / 2, w / 2, -w / 2 }, hy[4] = { -h / 2, -h / 2, h / 2, h / 2 };
  for (int i = 0; i < 4; i++) out[i].Set(cx + hx[i] * c - hy[i] * s, cy + hx[i] * s + hy[i] * c);
}

static void TestShapeCollision() {
  SECTION("shape-to-shape collision");
  const ddP S(-777.0f, -777.0f);
  RenderBlock b(1024 * 1024);
  ddP q1 = S, q2 = S;

  // ---- circles: outline vs disc semantics
  ddCircle *c1 = b.Emplace<ddCircle>(ddP(0, 0), 5.0f);
  ddCircle *c2 = b.Emplace<ddCircle>(ddP(6, 0), 5.0f);
  CHECK(ddCollide(c1, c2, &q1, &q2) == 2 && NearP(q1, 3, 4) && NearP(q2, 3, -4));
  CHECK(ddOverlap(c1, c2));
  c2->SetPos(20, 0);
  q1 = S; CHECK(ddCollide(c1, c2, &q1, &q2) == 0 && q1[0] == S[0] && !ddOverlap(c1, c2));
  c2->SetPos(0, 0); c2->Radius = 2.0f;                               // small unfilled circle inside a big unfilled one
  CHECK(ddCollide(c1, c2) == 0 && !ddOverlap(c1, c2));               // outlines never meet
  CHECK(ddOverlap(c1, c2, true));                                    // ...but they do occupy common space
  c1->Filled = true;                                                 // filled outer circle swallows the inner one
  CHECK(ddCollide(c1, c2) == 0 && ddOverlap(c1, c2));
  c1->Filled = false; c2->Radius = 5.0f; c2->SetPos(0, 0);
  CHECK(ddCollide(c1, c2) == DD_INFINITE || ddCollide(c1, c2) >= 1);   // identical circles: contact everywhere
  b.Clear();

  // ---- rectangles, rotation
  ddRect *ra = b.Emplace<ddRect>(ddP(0, 0), 10.0f, 10.0f);
  ddRect *rb = b.Emplace<ddRect>(ddP(5, 5), 10.0f, 10.0f);
  { ddP pts[8]; int n = ddContacts(ra, rb, pts, 8);
    CHECK(n == 2 && ((NearP(pts[0], 5, 0) && NearP(pts[1], 0, 5)) || (NearP(pts[1], 5, 0) && NearP(pts[0], 0, 5)))); }
  CHECK(ddOverlap(ra, rb));
  rb->SetPos(30, 0); CHECK(!ddOverlap(ra, rb) && ddCollide(ra, rb) == 0);
  rb->SetPos(0, 0); rb->W = rb->H = 4.0f;                            // small unfilled square inside a big one
  CHECK(!ddOverlap(ra, rb) && ddOverlap(ra, rb, true));
  ra->Filled = true; CHECK(ddOverlap(ra, rb));
  b.Clear();

  // ---- rotation of the shape changes the answer
  ddRect *bar = b.Emplace<ddRect>(ddP(0, 0), 10.0f, 2.0f);
  ddLine *ln = b.Emplace<ddLine>(ddP(-3, 4), ddP(3, 4));
  CHECK(!ddOverlap(bar, ln) && ddCollide(bar, ln) == 0);
  bar->SetAngle(DDD_DEG2RAD(90));                                    // now 2 wide, 10 tall
  q1 = q2 = S;
  { ddP pts[4]; int n = ddContacts(bar, ln, pts, 4);
    CHECK(n == 2 && fabsf(pts[0][1] - 4) < 1e-3f && fabsf(fabsf(pts[0][0]) - 1) < 1e-3f); }
  CHECK(ddCollide(bar, ln, &q1, &q2) == 2 && Near(q1[1], 4) && Near(q2[1], 4));
  b.Clear();

  // ---- non-uniform scale turns a circle into an ellipse
  ddCircle *ci = b.Emplace<ddCircle>(ddP(0, 0), 5.0f);
  ddLine *vl = b.Emplace<ddLine>(ddP(0, -6), ddP(0, 6));
  ddLine *hl = b.Emplace<ddLine>(ddP(-6, 0), ddP(6, 0));
  CHECK(ddCollide(ci, vl) == 2 && ddCollide(ci, hl) == 2);
  ci->SetScale(2.0f, 1.0f);                                          // 10 wide, 5 tall
  { ddP pts[4]; CHECK(ddCollide(ci, vl, &q1, &q2) == 2 && Near(fabsf(q1[1]), 5, 2e-2f));
    CHECK(ddContacts(ci, hl, pts, 4) == 0);                          // horizontal line stops at +-6 but the ellipse reaches +-10
    hl->B = ddP(12, 0); hl->A = ddP(-12, 0);
    CHECK(ddContacts(ci, hl, pts, 4) == 2 && Near(fabsf(pts[0][0]), 10, 2e-2f)); }
  b.Clear();

  // ---- concave polygon: the notch is empty, the arms are solid
  ddP star[10];
  for (int i = 0; i < 10; i++) { float a = DDD_DEG2RAD(90.0f + i * 36.0f), rad = (i & 1) ? 4.0f : 10.0f; star[i].Set(rad * cosf(a), rad * sinf(a)); }
  ddPolygon *st = b.EmplacePolygon(star, 10, TFT_WHITE, true, true);
  ddCircle *dot = b.Emplace<ddCircle>(ddP(0, 0), 0.5f, TFT_WHITE, true);                        // centre of the star
  CHECK(ddOverlap(st, dot));
  dot->SetPos(7.0f * cosf(DDD_DEG2RAD(126)), 7.0f * sinf(DDD_DEG2RAD(126))); dot->Radius = 0.2f;   // out in the notch between two arms
  CHECK(!ddOverlap(st, dot));                                                                  // inside the bounding box but not the shape
  dot->SetPos(0, 8);                                                                           // up the top arm
  CHECK(ddOverlap(st, dot));
  dot->SetPos(0, -3.0f); dot->Radius = 0.2f; CHECK(ddOverlap(st, dot));                         // inside the star body
  dot->SetPos(100, 0); CHECK(!ddOverlap(st, dot));
  b.Clear();

  // ---- arcs, arrows, beziers, markers, points
  ddArc *arc = b.Emplace<ddArc>(ddP(0, 0), 10.0f, 0.0f, DDD_PI);       // upper half circle
  ddLine *up = b.Emplace<ddLine>(ddP(0, 0), ddP(0, 20));
  ddLine *dn = b.Emplace<ddLine>(ddP(0, -1), ddP(0, -20));
  CHECK(ddCollide(arc, up, &q1) == 1 && NearP(q1, 0, 10, 2e-2f));
  CHECK(ddCollide(arc, dn) == 0);
  ddArrow *ar = b.Emplace<ddArrow>(ddP(-20, 5), ddP(20, 5), TFT_WHITE, 6.0f, 5.0f, true);
  CHECK(ddOverlap(ar, up) && ddOverlap(ar, arc));
  ddBezier *bz = b.Emplace<ddBezier>(ddP(-10, -10), ddP(0, 20), ddP(10, -10));       // quadratic hump peaking at y = 5
  ddLine *hz = b.Emplace<ddLine>(ddP(-20, 0), ddP(20, 0));
  CHECK(ddCollide(bz, hz) == 2);
  ddMarker *mk = b.Emplace<ddMarker>(ddP(0, 10), ddMarkerType::Plus, 4);
  CHECK(ddOverlap(mk, arc) && !ddOverlap(mk, dn));
  ddPoint *pt = b.Emplace<ddPoint>(ddP(0, 10), 0.5f);
  CHECK(ddOverlap(pt, arc) && ddCollide(pt, arc) == 2);                  // the disc's rim crosses the arc twice
  ddText *tx = b.Emplace<ddText>(ddP(0, 10), "hi");
  CHECK(!ddOverlap(tx, arc));                                          // text has no collision geometry
  b.Clear();

  // ---- groups: children keep their own transforms inside the group's
  ddGroup *g = b.EmplaceGroup(); g->SetPos(10, 0);
  g->Children.Emplace<ddCircle>(ddP(0, 0), 3.0f);
  ddCircle *kid = g->Children.Emplace<ddCircle>(ddP(0, 8), 1.0f); (void)kid;
  ddLine *probe = b.Emplace<ddLine>(ddP(7, -20), ddP(7, 20));          // x = 7 touches the first child's left edge, x = 10 would cross the middle
  CHECK(ddCollide(g, probe) >= 1 && ddOverlap(g, probe));
  probe->A = ddP(30, -20); probe->B = ddP(30, 20);
  CHECK(!ddOverlap(g, probe));
  g->SetPos(30, 0);                                                    // moving the group moves its children
  CHECK(ddOverlap(g, probe));
  g->SetAngle(DDD_DEG2RAD(90)); g->SetPos(0, 0);                       // child at local (0,8) swings to (-8,0)
  ddLine *hit = b.Emplace<ddLine>(ddP(-8, -5), ddP(-8, 5));
  CHECK(ddOverlap(g, hit));
  g->Visible = false; CHECK(!ddOverlap(g, hit) && ddCollide(g, hit) == 0);
  g->Visible = true; hit->Visible = false; CHECK(!ddOverlap(g, hit));
  b.Clear();

  // ---- parent matrices
  ddCircle *p1 = b.Emplace<ddCircle>(ddP(0, 0), 2.0f), *p2 = b.Emplace<ddCircle>(ddP(0, 0), 2.0f);
  Matrix3x3 shift(Matrix3x3::TransformType::TRANSLATION, 3.0f, 0.0f);
  CHECK(ddOverlap(p1, p2) && ddOverlap(p1, p2, false, shift, shift));
  CHECK(ddCollide(p1, p2, nullptr, nullptr, false, Matrix3x3(), shift) == 2);   // b moved 3 to the right: still crossing
  Matrix3x3 far(Matrix3x3::TransformType::TRANSLATION, 30.0f, 0.0f);
  CHECK(!ddOverlap(p1, p2, false, Matrix3x3(), far));
  b.Clear();

  // ---- finding everything a shape touches
  SECTION("shape-to-shape: scene queries");
  ddRect *probeR = b.Emplace<ddRect>(ddP(0, 0), 10.0f, 10.0f);
  ddCircle *inside = b.Emplace<ddCircle>(ddP(2, 2), 1.0f, TFT_WHITE, true);
  ddCircle *crossing = b.Emplace<ddCircle>(ddP(5, 0), 2.0f);
  ddLine *across = b.Emplace<ddLine>(ddP(-10, -10), ddP(10, 10));
  ddCircle *away = b.Emplace<ddCircle>(ddP(50, 50), 3.0f);
  ddCircle *hidden = b.Emplace<ddCircle>(ddP(1, 1), 1.0f, TFT_WHITE, true); hidden->Visible = false;
  ddText *label = b.Emplace<ddText>(ddP(0, 0), "x");
  (void)away; (void)label;
  ddVectorPrimitive *found[16];
  int n = ddFindOverlaps(b, probeR, found, 16, true);
  bool hasIn = false, hasCross = false, hasAcross = false, hasAway = false, hasHidden = false, hasSelf = false;
  for (int i = 0; i < n; i++) { hasIn |= found[i] == inside; hasCross |= found[i] == crossing; hasAcross |= found[i] == across; hasAway |= found[i] == away; hasHidden |= found[i] == hidden; hasSelf |= found[i] == probeR; }
  CHECK(n == 3 && hasIn && hasCross && hasAcross && !hasAway && !hasHidden && !hasSelf);
  n = ddFindOverlaps(b, probeR, found, 16, false);                      // outline only: the filled circle inside is not touching the rectangle's outline
  hasIn = false; for (int i = 0; i < n; i++) hasIn |= found[i] == inside;
  CHECK(n == 2 && !hasIn);
  CHECK(ddFindOverlaps(b, probeR, found, 1, true) == 1);                // capped
  b.Clear();

  // ---- randomised: agree with independent reference implementations
  SECTION("shape-to-shape: randomised vs reference");
  unsigned seed = 7;
  auto rnd = [&](float lo, float hi) { seed = seed * 1664525u + 1013904223u; return lo + (hi - lo) * ((seed >> 8) / 16777216.0f); };
  int cc = 0, rr = 0, rcir = 0, skipped = 0, bad = 0;
  for (int i = 0; i < 6000; i++) {
    b.Clear();
    // filled circle vs filled circle: exact formula
    float x1 = rnd(-20, 20), y1 = rnd(-20, 20), x2 = rnd(-20, 20), y2 = rnd(-20, 20), ra1 = rnd(1, 12), ra2 = rnd(1, 12);
    float gap = hypotf(x1 - x2, y1 - y2) - ra1 - ra2;
    ddCircle *a = b.Emplace<ddCircle>(ddP(x1, y1), ra1, TFT_WHITE, true), *c = b.Emplace<ddCircle>(ddP(x2, y2), ra2, TFT_WHITE, true);
    if (fabsf(gap) > 1e-2f) { cc++; if (ddOverlap(a, c) != (gap < 0)) bad++; } else skipped++;
    // two rotated rectangles (filled) vs separating-axis
    float w1 = rnd(2, 14), h1 = rnd(2, 14), w2 = rnd(2, 14), h2 = rnd(2, 14), an1 = rnd(0, 6.28f), an2 = rnd(0, 6.28f);
    b.Clear();
    ddRect *r1 = b.Emplace<ddRect>(ddP(x1, y1), w1, h1, TFT_WHITE, true), *r2 = b.Emplace<ddRect>(ddP(x2, y2), w2, h2, TFT_WHITE, true);
    r1->SetAngle(an1); r2->SetAngle(an2);
    ddP A[4], B[4]; RectCorners(x1, y1, w1, h1, an1, A); RectCorners(x2, y2, w2, h2, an2, B);
    bool sat = SatOverlap(A, B);
    // skip razor-thin cases: shrink/grow by 0.02 and require the answer to be stable
    ddP A2[4], B2[4]; RectCorners(x1, y1, w1 + 0.04f, h1 + 0.04f, an1, A2); RectCorners(x2, y2, w2 + 0.04f, h2 + 0.04f, an2, B2);
    ddP A3[4], B3[4]; RectCorners(x1, y1, w1 - 0.04f, h1 - 0.04f, an1, A3); RectCorners(x2, y2, w2 - 0.04f, h2 - 0.04f, an2, B3);
    if (SatOverlap(A2, B2) == SatOverlap(A3, B3)) { rr++; if (ddOverlap(r1, r2) != sat) bad++; } else skipped++;
    // rotated filled rect vs filled circle: closest point on the box in the rectangle's own frame
    b.Clear();
    ddRect *r3 = b.Emplace<ddRect>(ddP(x1, y1), w1, h1, TFT_WHITE, true);
    ddCircle *c3 = b.Emplace<ddCircle>(ddP(x2, y2), ra2, TFT_WHITE, true);
    r3->SetAngle(an1);
    float cs = cosf(an1), sn = sinf(an1), lx = (x2 - x1) * cs + (y2 - y1) * sn, ly = -(x2 - x1) * sn + (y2 - y1) * cs;
    float dx = fmaxf(fabsf(lx) - w1 / 2, 0.0f), dy = fmaxf(fabsf(ly) - h1 / 2, 0.0f), dist = hypotf(dx, dy);
    if (fabsf(dist - ra2) > 1e-2f) { rcir++; if (ddOverlap(r3, c3) != (dist < ra2)) bad++; } else skipped++;
    // outline contact points really lie on both outlines
    b.Clear();
    ddCircle *oc = b.Emplace<ddCircle>(ddP(x1, y1), ra1);
    ddRect *orc = b.Emplace<ddRect>(ddP(x2, y2), w2, h2); orc->SetAngle(an2);
    ddP pts[16]; int k = ddContacts(oc, orc, pts, 16);
    for (int j = 0; j < k; j++) {
      float cs2 = cosf(an2), sn2 = sinf(an2), px = pts[j][0] - x2, py = pts[j][1] - y2;
      float qx = px * cs2 + py * sn2, qy = -px * sn2 + py * cs2;
      bool onRim = fabsf(pts[j].DistanceTo(ddP(x1, y1)) - ra1) < 2e-3f;
      bool onBox = fabsf(qx) <= w2 / 2 + 2e-3f && fabsf(qy) <= h2 / 2 + 2e-3f && (fabsf(fabsf(qx) - w2 / 2) < 2e-3f || fabsf(fabsf(qy) - h2 / 2) < 2e-3f);
      if (!onRim || !onBox) bad++;
    }
  }
  printf("  random: %d circle/circle, %d rect/rect (SAT), %d rect/circle checked, %d razor-thin skipped, %d disagreements\n", cc, rr, rcir, skipped, bad);
  CHECK(bad == 0 && cc > 4000 && rr > 3000 && rcir > 4000);
}

static void Stress() {
  SECTION("stress");
  LGFX_Sprite spr; spr.setColorDepth(16); spr.createSprite(1280, 960);
  ddR r(&spr, -160.0f, 160.0f, -120.0f, 120.0f);
  RenderBlock b(8 * 1024 * 1024);
  unsigned seed = 99;
  auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };

  const int N = 6000;
  auto make = [&](int type, bool onscreenBias) -> ddVectorPrimitive * {
    float x = (rnd() - 0.5f) * (onscreenBias ? 300.0f : 800.0f), y = (rnd() - 0.5f) * (onscreenBias ? 220.0f : 600.0f);
    uint16_t col = (uint16_t)(0x1000 + (seed >> 5) % 0xEFFF);
    ddVectorPrimitive *p = nullptr;
    switch (type) {
      case 0: p = b.Emplace<ddCircle>(ddP(x, y), 2.0f + rnd() * 6.0f, col, rnd() > 0.5f); break;
      case 1: { auto *e = b.Emplace<ddRect>(ddP(x, y), 4.0f + rnd() * 10.0f, 3.0f + rnd() * 8.0f, col, rnd() > 0.5f); if (e) e->SetAngle(rnd() * 6.28f); p = e; break; }
      case 2: p = b.Emplace<ddNGon>(ddP(x, y), 3 + (int)(rnd() * 6), 3.0f + rnd() * 6.0f, col, rnd() > 0.5f); break;
      case 3: p = b.Emplace<ddLine>(ddP(x, y), ddP(x + rnd() * 20.0f - 10.0f, y + rnd() * 20.0f - 10.0f), col); break;
      case 4: p = b.Emplace<ddPoint>(ddP(x, y), 1.0f, col); break;
      case 5: { auto *e = b.Emplace<ddEllipse>(ddP(x, y), 3.0f + rnd() * 6.0f, 2.0f + rnd() * 4.0f, col); if (e) e->SetAngle(rnd() * 6.28f); p = e; break; }
    }
    if (p) p->Tag = (uint16_t)type;
    return p;
  };
  for (int i = 0; i < N; i++) make(i % 6, false);   // most are off-screen
  CHECK(b.Count() == N);
  printf("  %d objects, arena in use %zu KB (capacity %zu KB)\n", N, b.Arena()->InUse() / 1024, b.Arena()->Capacity() / 1024);

  auto frame = [&](bool cull) {
    r.CullEnabled = cull; r.ResetStats();
    spr.clear(); b.Draw(&r);
  };
  frame(true);
  const int F = 30;
  auto t0 = Clock::now(); for (int i = 0; i < F; i++) frame(true);  double tc = MsSince(t0) / F;
  t0 = Clock::now();      for (int i = 0; i < F; i++) frame(false); double tn = MsSince(t0) / F;
  frame(true);
  printf("  culling ON : %.2f ms/frame  (drawn %u, culled %u)\n", tc, r.Drawn, r.Culled);
  printf("  culling OFF: %.2f ms/frame\n", tn);
  CHECK(r.Culled > 0 && r.Drawn + r.Culled == (uint32_t)N);

  // animate + churn: every frame move some objects and replace some
  std::vector<ddVectorPrimitive *> live;
  for (ddVectorPrimitive *p = b.First(); p; p = b.Next(p)) live.push_back(p);
  size_t cap = b.Arena()->Capacity(), inuse = b.Arena()->InUse();
  t0 = Clock::now();
  for (int f = 0; f < 200; f++) {
    for (int k = 0; k < 100; k++) {                   // churn
      size_t idx = (size_t)(rnd() * live.size()) % live.size();
      int type = live[idx]->Tag;
      b.Remove(live[idx]);
      ddVectorPrimitive *np = make(type, false);    // same kind in, same kind out
      CHECK(np != nullptr);
      live[idx] = np;
    }
    for (size_t k = 0; k < live.size(); k += 7) live[k]->SetAngle(f * 0.1f);   // transform updates
    frame(true);
  }
  printf("  200 frames of churn+animation: %.2f ms/frame; capacity %zu -> %zu KB, in use %zu -> %zu KB\n",
         MsSince(t0) / 200.0, cap / 1024, b.Arena()->Capacity() / 1024, inuse / 1024, b.Arena()->InUse() / 1024);
  CHECK(b.Arena()->Capacity() <= cap + 2 * DD_ARENA_CHUNK);
  CHECK(b.Count() == N);
  CHECK(!b.Arena()->Overflowed());

  // mixed-kind churn: informational. Recycled blocks only serve their own (or slightly larger)
  // size class, so capacity can creep up when the mix of shapes changes a lot.
  {
    size_t cap2 = b.Arena()->Capacity();
    for (int f = 0; f < 200; f++)
      for (int k = 0; k < 100; k++) {
        size_t idx = (size_t)(rnd() * live.size()) % live.size();
        b.Remove(live[idx]);
        live[idx] = make((int)(rnd() * 6) % 6, false);
        if (!live[idx]) { CHECK(false); return; }
      }
    printf("  mixed-kind churn (20000 swaps): capacity %zu -> %zu KB\n", cap2 / 1024, b.Arena()->Capacity() / 1024);
  }

  // cap enforcement: a tiny block must refuse cleanly rather than crash
  RenderBlock tiny(2048);
  int made = 0;
  while (tiny.Emplace<ddCircle>(ddP(0, 0), 1.0f)) { if (++made > 10000) break; }
  CHECK(made < 10000 && tiny.Arena()->Overflowed());
  printf("  2 KB cap accepted %d circles then refused\n", made);
}

int main() {
  TestArena();
  TestScene();
  TestRender();
  TestInteract();
  TestSDLInput();
  TestGeom();
  TestBox();
  TestShapeCollision();
  Stress();
  printf("\n%d checks, %d failures\n", Checks, Failures);
  return Failures ? 1 : 0;
}
