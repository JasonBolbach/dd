#pragma once
// Scene primitives. Every primitive has:
//   - its own modifiable transform (SetPos / SetAngle / SetScale / Transform()),
//   - a Visible flag (hidden primitives skip transform + render entirely),
//   - automatic culling: primitives with known bounds that are off-screen are skipped,
//   - a Pen (width / dashed / dotted) for line-based shapes,
//   - shape-specific public fields you can change at any time,
//   - HitTest() for picking, plus Draggable/Tag for the interaction layer (ddInteract.h).
// Geometry is defined around the primitive's LOCAL origin; position it with the transform
// (constructors that take a centre/position do this for you).
// Primitives live in a ddArena: never `delete` them; use RenderBlock::Remove / Clear / Reset.
// A primitive must derive (singly) from ddVectorPrimitive so its address equals the arena block.
#include <memory>
#include "ddArena.h"
#include "ddRender.h"
#include "ddGeom.h"
#include "ddShapeSet.h"

// ---------------------------------------------------------------- helpers
inline float ddMeanScale(const Matrix3x3 &m) {
  return sqrtf(fabsf(m.M[0][0]*m.M[1][1] - m.M[0][1]*m.M[1][0]));
}
// Kept for the shapes' HitLocal(); the real implementations live in ddGeom.h.
inline float ddSegDist(const ddP &p, const ddP &a, const ddP &b) { return ddDistPointLine(p, a, b); }
inline bool  ddInPoly(const ddP &p, const ddP *v, int n) { return ddPointInPolygon(p, v, n); }
// Segment count from an on-screen length in pixels (~4 px per segment).
inline int ddAutoSteps(float pxLength, int lo = 12, int hi = 180) {
  int s = (int)(pxLength / 4.0f);
  return s < lo ? lo : (s > hi ? hi : s);
}
// Axis-aligned box of a box pushed through matrix m.
inline void ddBoxThrough(const Matrix3x3 &m, float x0, float y0, float x1, float y1,
                         float &ox0, float &oy0, float &ox1, float &oy1) {
  const float xs[4] = { x0, x1, x1, x0 }, ys[4] = { y0, y0, y1, y1 };
  for (int i = 0; i < 4; i++) {
    float x = m.M[0][0]*xs[i] + m.M[0][1]*ys[i] + m.M[0][2];
    float y = m.M[1][0]*xs[i] + m.M[1][1]*ys[i] + m.M[1][2];
    if (i == 0) { ox0 = ox1 = x; oy0 = oy1 = y; }
    else { if (x < ox0) ox0 = x; if (x > ox1) ox1 = x; if (y < oy0) oy0 = y; if (y > oy1) oy1 = y; }
  }
}

// Is m a similarity (uniform scale with any rotation / reflection)? s receives the scale.
inline bool ddIsSimilarity(const Matrix3x3 &m, float &s) {
  float sx = hypotf(m.M[0][0], m.M[1][0]), sy = hypotf(m.M[0][1], m.M[1][1]);
  float dot = m.M[0][0]*m.M[0][1] + m.M[1][0]*m.M[1][1];
  s = sx;
  return fabsf(sx - sy) < 1e-4f * (sx + sy) && fabsf(dot) < 1e-4f * sx * sy;
}
// The unit circle pushed through m, as collision geometry: a true circle when m is a similarity,
// otherwise (non-uniform scale / skew) a polygon approximation of the resulting ellipse.
inline void ddAddUnitCircleShape(const Matrix3x3 &m, ddShapeSet &out, bool filled, int steps = 48) {
  float s;
  if (ddIsSimilarity(m, s)) { out.AddCircle(m * ddP(0, 0), s, filled); return; }
  out.BeginPoly(true, filled);
  for (int i = 0; i < steps; i++) { float a = 2.0f * DDD_PI * i / steps; out.PolyPush(m * ddP(cosf(a), sinf(a))); }
  out.EndPoly();
}

class RenderBlock;

// ---------------------------------------------------------------- base
struct ddVectorPrimitive {
  uint16_t Color = TFT_WHITE;
  bool     Visible = true;
  bool     Draggable = false;   // used by ddInteract
  uint16_t Tag = 0;             // free for the application (e.g. button ids)
  ddPen    Pen;                 // stroke for line-based shapes

  // Transform. Setters mark it dirty; the matrix is rebuilt once at render time.
  void SetPos(float x, float y)   { Xf.SetTranslation(x, y); Touch(); }
  void SetPos(const ddP &p)       { SetPos(p[0], p[1]); }
  void SetAngle(float radians)    { Xf.SetRotation(radians); Touch(); }
  void SetScale(float x, float y) { Xf.SetScale(x, y); Touch(); }
  void SetScale(float s)          { SetScale(s, s); }
  void Translate(float dx, float dy) { Xf.SetTranslation(Xf.Tx + dx, Xf.Ty + dy); Touch(); }
  ddT &Transform()                { Touch(); return Xf; }   // direct access; assumed modified
  const ddT &GetTransform() const { return Xf; }

  // Draws with `parent` (world-from-parent) applied. No-op when hidden; skipped when off-screen.
  void Render(const Matrix3x3 &parent, ddR *r) {
    if (!Visible) return;
    const Matrix3x3 *mp = &parent;
    Matrix3x3 tmp;
    if (UsesXf) {
      if (Dirty) { Xf.Combine(); Dirty = false; }
      tmp = parent * Xf.M;
      mp = &tmp;
    }
    if (r->CullEnabled) {
      float x0, y0, x1, y1;
      if (LocalBounds(x0, y0, x1, y1)) {
        float wx0, wy0, wx1, wy1;
        ddBoxThrough(*mp, x0, y0, x1, y1, wx0, wy0, wx1, wy1);
        if (!r->InView(wx0, wy0, wx1, wy1, 3.0f + Pen.Width)) { r->Culled++; return; }
      }
    }
    r->Drawn++;
    DrawShape(*mp, r);
  }

  // p is in the PARENT's space (world space for top-level primitives); tol is in the same units.
  bool HitTest(const ddP &p, float tol = 0.0f) {
    if (!UsesXf) return HitLocal(p, tol);
    if (Dirty) { Xf.Combine(); Dirty = false; }
    float s = ddMeanScale(Xf.M);
    if (s < 1e-9f) return false;
    Matrix3x3 inv = Xf.M; inv.Invert();
    return HitLocal(inv * p, tol / s);
  }

  // Appends this primitive's collision geometry in WORLD space (parent = world-from-parent).
  // Hidden primitives add nothing.
  void CollectShapes(const Matrix3x3 &parent, ddShapeSet &out) {
    if (!Visible) return;
    if (!UsesXf) { Shapes(parent, out); return; }
    if (Dirty) { Xf.Combine(); Dirty = false; }
    Shapes(parent * Xf.M, out);
  }

  // Bounding box in the PARENT's space (world space for top-level primitives).
  // Returns false if the shape has no known bounds.
  bool ParentBounds(float &x0, float &y0, float &x1, float &y1) {
    float lx0, ly0, lx1, ly1;
    if (!LocalBounds(lx0, ly0, lx1, ly1)) return false;
    if (!UsesXf) { x0 = lx0; y0 = ly0; x1 = lx1; y1 = ly1; return true; }
    if (Dirty) { Xf.Combine(); Dirty = false; }
    ddBoxThrough(Xf.M, lx0, ly0, lx1, ly1, x0, y0, x1, y1);
    return true;
  }

 protected:
  void Touch() { UsesXf = true; Dirty = true; }
  virtual void DrawShape(const Matrix3x3 &m, ddR *r) = 0;                 // m = world-from-local
  virtual bool HitLocal(const ddP &, float) { return false; }              // point in local space
  virtual void Shapes(const Matrix3x3 &, ddShapeSet &) {}                  // collision geometry; m = world-from-local
  // Local-space bounds. Return false for "unknown / unbounded" (never culled).
  virtual bool LocalBounds(float &, float &, float &, float &) { return false; }

 private:
  friend class RenderBlock;
  ddT  Xf;
  bool UsesXf = false, Dirty = false;
  ddVectorPrimitive *Next = nullptr, *Prev = nullptr;
};

// ---------------------------------------------------------------- RenderBlock
// An ordered list of primitives (draw order = insertion order, last is on top).
// A top-level block owns an arena (grown on demand up to maxBytes). A block constructed from
// an existing arena (used by ddGroup) shares it.
class ddGroup;
struct ddPolygon;

class RenderBlock {
  ddArena *A;
  std::unique_ptr<ddArena> Own;
  ddVectorPrimitive *Head = nullptr, *Tail = nullptr;
  int N = 0;

  void Link(ddVectorPrimitive *p) {
    p->Prev = Tail; p->Next = nullptr;
    if (Tail) Tail->Next = p; else Head = p;
    Tail = p; N++;
  }
  void Unlink(ddVectorPrimitive *p) {
    if (p->Prev) p->Prev->Next = p->Next; else Head = p->Next;
    if (p->Next) p->Next->Prev = p->Prev; else Tail = p->Prev;
    p->Next = p->Prev = nullptr; N--;
  }

 public:
  explicit RenderBlock(size_t maxBytes = DD_ARENA_DEFAULT_MAX) : Own(new ddArena(maxBytes)) { A = Own.get(); }
  explicit RenderBlock(ddArena *shared) : A(shared) {}
  RenderBlock(const RenderBlock &) = delete;
  RenderBlock &operator=(const RenderBlock &) = delete;

  ddArena *Arena() { return A; }

  // Constructs a primitive in the arena. Returns nullptr if the arena is full.
  template <class T, class... Args>
  T *Emplace(Args &&...args) {
    T *p = A->New<T>(std::forward<Args>(args)...);
    if (p) Link(p);
    return p;
  }
  // Polygon / polyline whose points are copied into the arena (the source array can go away).
  ddPolygon *EmplacePolygon(const ddP *pts, int n, uint16_t color = TFT_WHITE, bool closed = true, bool filled = false);
  ddGroup   *EmplaceGroup();

  // Unlink without freeing (the pointer stays valid; memory is reclaimed at Clear/Reset).
  void Detach(ddVectorPrimitive *p) { if (p) Unlink(p); }
  // Unlink, destroy and recycle the memory. The pointer is invalid afterwards.
  void Remove(ddVectorPrimitive *p) {
    if (!p) return;
    Unlink(p);
    A->Delete(p);
  }
  // Remove everything. An owning block resets its arena (chunks are kept for reuse);
  // a block sharing an arena (group children) destroys and recycles each of its primitives.
  void Clear() {
    if (Own) { A->Reset(); Head = Tail = nullptr; N = 0; return; }
    if (A->IsResetting()) { Head = Tail = nullptr; N = 0; return; }   // arena destroys them itself
    for (ddVectorPrimitive *p = Head; p;) { ddVectorPrimitive *nx = p->Next; A->Delete(p); p = nx; }
    Head = Tail = nullptr; N = 0;
  }
  void Reset() { Clear(); }

  void MoveToFront(ddVectorPrimitive *p) { if (p && p != Tail) { Unlink(p); Link(p); } }
  void MoveToBack(ddVectorPrimitive *p) {
    if (!p || p == Head) return;
    Unlink(p);
    p->Next = Head; p->Prev = nullptr;
    if (Head) Head->Prev = p; else Tail = p;
    Head = p; N++;
  }

  int Count() const { return N; }
  ddVectorPrimitive *First() const { return Head; }
  ddVectorPrimitive *Next(const ddVectorPrimitive *p) const { return p->Next; }

  void Draw(ddR *r, const Matrix3x3 &parent = Matrix3x3()) {
    for (ddVectorPrimitive *p = Head; p; p = p->Next) p->Render(parent, r);
  }

  // Topmost visible primitive under world point `p` (within `tol` world units), or nullptr.
  ddVectorPrimitive *Pick(const ddP &p, float tol = 0.0f) {
    for (ddVectorPrimitive *q = Tail; q; q = q->Prev)
      if (q->Visible && q->HitTest(p, tol)) return q;
    return nullptr;
  }

  // Union of the children's bounds (in this block's space). False if empty or any child is unbounded.
  bool Bounds(float &x0, float &y0, float &x1, float &y1) {
    bool any = false;
    for (ddVectorPrimitive *p = Head; p; p = p->Next) {
      if (!p->Visible) continue;
      float a, b, c, d;
      if (!p->ParentBounds(a, b, c, d)) return false;
      if (!any) { x0 = a; y0 = b; x1 = c; y1 = d; any = true; }
      else { if (a < x0) x0 = a; if (b < y0) y0 = b; if (c > x1) x1 = c; if (d > y1) y1 = d; }
    }
    return any;
  }
};

// ---------------------------------------------------------------- shapes
struct ddPoint : ddVectorPrimitive {
  float Radius;     // world units
  bool  Label;      // draw world coordinates above the point (needs DD_DEBUG)
  ddPoint(ddP p, float rad = 1, uint16_t color = TFT_WHITE, bool label = false) : Radius(rad), Label(label) {
    Color = color; SetPos(p);
  }
 protected:
  void Shapes(const Matrix3x3 &m, ddShapeSet &out) override { out.AddCircle(m * ddP(0, 0), Radius * ddMeanScale(m), true); }
  void DrawShape(const Matrix3x3 &m, ddR *r) override {
    ddP c = (r->W2D * m) * ddP(0, 0);
    r->DrawPoint(c, (int)lroundf(Radius * ddMeanScale(m) * r->PixelsPerUnit()), Color, false, Label);
  }
  bool HitLocal(const ddP &p, float tol) override { return hypotf(p[0], p[1]) <= Radius + tol; }
  bool LocalBounds(float &x0, float &y0, float &x1, float &y1) override { x0 = y0 = -Radius; x1 = y1 = Radius; return true; }
};

struct ddLine : ddVectorPrimitive {
  ddP A, B;
  ddLine(ddP a, ddP b, uint16_t color = TFT_WHITE) : A(a), B(b) { Color = color; }
 protected:
  void Shapes(const Matrix3x3 &m, ddShapeSet &out) override { out.AddSegment(m * A, m * B); }
  void DrawShape(const Matrix3x3 &m, ddR *r) override {
    Matrix3x3 f = r->W2D * m;
    r->DrawLine(f * A, f * B, Color, false, Pen);
  }
  bool HitLocal(const ddP &p, float tol) override { return ddSegDist(p, A, B) <= tol; }
  bool LocalBounds(float &x0, float &y0, float &x1, float &y1) override {
    x0 = fminf(A[0], B[0]); x1 = fmaxf(A[0], B[0]); y0 = fminf(A[1], B[1]); y1 = fmaxf(A[1], B[1]); return true;
  }
};

// Unit circle through matrix m. Uses LovyanGFX's circle/ellipse primitives when the result is
// axis-aligned on screen (uniform scale + any rotation, or axis scale incl. 90 degree turns) and
// the pen is plain; otherwise a polyline whose segment count follows on-screen size
// (steps <= 0 = automatic), which also honours the pen.
inline void ddDrawUnitCircle(const Matrix3x3 &m, ddR *r, uint16_t color, int steps, bool filled, const ddPen &pen) {
  const float eps = 1e-4f;
  float sx  = hypotf(m.M[0][0], m.M[1][0]);
  float sy  = hypotf(m.M[0][1], m.M[1][1]);
  float dot = m.M[0][0]*m.M[0][1] + m.M[1][0]*m.M[1][1];
  float ppu = r->PixelsPerUnit();
  ddP c = m * ddP(0, 0);
  bool fast = filled || pen.Plain();

  if (fast && fabsf(sx - sy) < eps * (sx + sy) && fabsf(dot) < eps * sx * sy) {
    int rad = (int)lroundf(sx * ppu);
    if (filled) r->FillCircle(c, rad, color, true); else r->DrawCircle(c, rad, color, true);
  } else if (fast && fabsf(m.M[0][1]) < eps && fabsf(m.M[1][0]) < eps) {
    int rx = (int)lroundf(sx * ppu), ry = (int)lroundf(sy * ppu);
    if (filled) r->FillEllipse(c, rx, ry, color, true); else r->DrawEllipse(c, rx, ry, color, true);
  } else if (fast && fabsf(m.M[0][0]) < eps && fabsf(m.M[1][1]) < eps) {
    int rx = (int)lroundf(sy * ppu), ry = (int)lroundf(sx * ppu);
    if (filled) r->FillEllipse(c, rx, ry, color, true); else r->DrawEllipse(c, rx, ry, color, true);
  } else {
    steps = steps <= 0 ? ddAutoSteps(2.0f * DDD_PI * fmaxf(sx, sy) * ppu) : (steps < 3 ? 3 : steps);
    Matrix3x3 f = r->W2D * m;
    r->Tmp.clear();
    for (int i = 0; i < steps; i++) {
      float a = 2.0f * DDD_PI * i / steps;
      r->Tmp.push_back(f * ddP(cosf(a), sinf(a)));
    }
    r->DrawPolyScreen(r->Tmp.data(), steps, true, filled, color, pen);
  }
}

struct ddCircle : ddVectorPrimitive {
  float Radius;   // world units
  bool  Filled;
  int   Steps;    // fallback segment count; 0 = automatic
  ddCircle(ddP c, float radius, uint16_t color = TFT_WHITE, bool filled = false, int steps = 0)
    : Radius(radius), Filled(filled), Steps(steps) { Color = color; SetPos(c); }
 protected:
  void Shapes(const Matrix3x3 &m, ddShapeSet &out) override {
    ddAddUnitCircleShape(m * Matrix3x3(Matrix3x3::TransformType::SCALE, Radius, Radius), out, Filled);
  }
  void DrawShape(const Matrix3x3 &m, ddR *r) override {
    ddDrawUnitCircle(m * Matrix3x3(Matrix3x3::TransformType::SCALE, Radius, Radius), r, Color, Steps, Filled, Pen);
  }
  bool HitLocal(const ddP &p, float tol) override {
    float d = hypotf(p[0], p[1]);
    return Filled ? d <= Radius + tol : fabsf(d - Radius) <= tol;
  }
  bool LocalBounds(float &x0, float &y0, float &x1, float &y1) override { x0 = y0 = -Radius; x1 = y1 = Radius; return true; }
};

struct ddEllipse : ddVectorPrimitive {
  float Rx, Ry;   // world units; rotate with SetAngle()
  bool  Filled;
  int   Steps;
  ddEllipse(ddP c, float rx, float ry, uint16_t color = TFT_WHITE, bool filled = false, int steps = 0)
    : Rx(rx), Ry(ry), Filled(filled), Steps(steps) { Color = color; SetPos(c); }
 protected:
  void Shapes(const Matrix3x3 &m, ddShapeSet &out) override {
    ddAddUnitCircleShape(m * Matrix3x3(Matrix3x3::TransformType::SCALE, Rx, Ry), out, Filled);
  }
  void DrawShape(const Matrix3x3 &m, ddR *r) override {
    ddDrawUnitCircle(m * Matrix3x3(Matrix3x3::TransformType::SCALE, Rx, Ry), r, Color, Steps, Filled, Pen);
  }
  bool HitLocal(const ddP &p, float tol) override {
    if (Rx <= 0.0f || Ry <= 0.0f) return false;
    float q = hypotf(p[0] / Rx, p[1] / Ry);
    float t = tol / fminf(Rx, Ry);
    return Filled ? q <= 1.0f + t : fabsf(q - 1.0f) <= t;
  }
  bool LocalBounds(float &x0, float &y0, float &x1, float &y1) override { x0 = -Rx; x1 = Rx; y0 = -Ry; y1 = Ry; return true; }
};

struct ddRect : ddVectorPrimitive {
  float W, H;     // world units, centred on the local origin
  bool  Filled;
  ddRect(ddP c, float w, float h, uint16_t color = TFT_WHITE, bool filled = false)
    : W(w), H(h), Filled(filled) { Color = color; SetPos(c); }
 protected:
  void Shapes(const Matrix3x3 &m, ddShapeSet &out) override {
    float hw = W * 0.5f, hh = H * 0.5f;
    out.BeginPoly(true, Filled);
    out.PolyPush(m * ddP(-hw, -hh)); out.PolyPush(m * ddP(hw, -hh)); out.PolyPush(m * ddP(hw, hh)); out.PolyPush(m * ddP(-hw, hh));
    out.EndPoly();
  }
  void DrawShape(const Matrix3x3 &m, ddR *r) override {
    const float eps = 1e-4f;
    float hw = W * 0.5f, hh = H * 0.5f, ppu = r->PixelsPerUnit();
    Matrix3x3 f = r->W2D * m;
    float ehw = 0.0f, ehh = 0.0f; bool axis = Filled || Pen.Plain();
    if (axis) {
      if (fabsf(m.M[0][1]) < eps && fabsf(m.M[1][0]) < eps)      { ehw = fabsf(m.M[0][0]) * hw * ppu; ehh = fabsf(m.M[1][1]) * hh * ppu; }
      else if (fabsf(m.M[0][0]) < eps && fabsf(m.M[1][1]) < eps) { ehw = fabsf(m.M[0][1]) * hh * ppu; ehh = fabsf(m.M[1][0]) * hw * ppu; }
      else axis = false;
    }
    if (axis) {
      ddP c = f * ddP(0, 0);
      int x0 = lroundf(c[0] - ehw), y0 = lroundf(c[1] - ehh);
      int w = lroundf(2.0f * ehw), h = lroundf(2.0f * ehh);
      if (Filled) r->Sprite->fillRect(x0, y0, w, h, Color); else r->Sprite->drawRect(x0, y0, w, h, Color);
    } else {
      r->Tmp.clear();
      r->Tmp.push_back(f * ddP(-hw, -hh)); r->Tmp.push_back(f * ddP( hw, -hh));
      r->Tmp.push_back(f * ddP( hw,  hh)); r->Tmp.push_back(f * ddP(-hw,  hh));
      r->DrawPolyScreen(r->Tmp.data(), 4, true, Filled, Color, Pen);
    }
  }
  bool HitLocal(const ddP &p, float tol) override {
    float hw = W * 0.5f, hh = H * 0.5f;
    bool outer = fabsf(p[0]) <= hw + tol && fabsf(p[1]) <= hh + tol;
    if (Filled || !outer) return outer;
    return !(fabsf(p[0]) < hw - tol && fabsf(p[1]) < hh - tol);
  }
  bool LocalBounds(float &x0, float &y0, float &x1, float &y1) override { x0 = -W * 0.5f; x1 = W * 0.5f; y0 = -H * 0.5f; y1 = H * 0.5f; return true; }
};

// Polygon (closed) or polyline (open). Points are NOT owned unless created through
// RenderBlock::EmplacePolygon, which copies them into the arena (and frees them with the polygon).
struct ddPolygon : ddVectorPrimitive {
  const ddP *Pts;
  int  Count;
  bool Closed, Filled;
  ddArena *Owner = nullptr;   // set when Pts lives in the arena
  ddPolygon(const ddP *pts, int n, uint16_t color = TFT_WHITE, bool closed = true, bool filled = false)
    : Pts(pts), Count(n), Closed(closed), Filled(filled) { Color = color; }
  ~ddPolygon() { if (Owner) Owner->Delete((void *)Pts); }
 protected:
  void Shapes(const Matrix3x3 &m, ddShapeSet &out) override {
    out.BeginPoly(Closed, Filled);
    for (int i = 0; i < Count; i++) out.PolyPush(m * Pts[i]);
    out.EndPoly();
  }
  void DrawShape(const Matrix3x3 &m, ddR *r) override {
    Matrix3x3 f = r->W2D * m;
    r->Tmp.clear();
    for (int i = 0; i < Count; i++) r->Tmp.push_back(f * Pts[i]);
    r->DrawPolyScreen(r->Tmp.data(), Count, Closed, Filled, Color, Pen);
  }
  bool HitLocal(const ddP &p, float tol) override {
    if (Count < 1) return false;
    if (Filled && Count >= 3 && ddInPoly(p, Pts, Count)) return true;
    for (int i = 0; i + 1 < Count; i++) if (ddSegDist(p, Pts[i], Pts[i + 1]) <= tol) return true;
    return Closed && Count > 2 && ddSegDist(p, Pts[Count - 1], Pts[0]) <= tol;
  }
  bool LocalBounds(float &x0, float &y0, float &x1, float &y1) override {
    if (Count < 1) return false;
    x0 = x1 = Pts[0][0]; y0 = y1 = Pts[0][1];
    for (int i = 1; i < Count; i++) {
      if (Pts[i][0] < x0) x0 = Pts[i][0]; if (Pts[i][0] > x1) x1 = Pts[i][0];
      if (Pts[i][1] < y0) y0 = Pts[i][1]; if (Pts[i][1] > y1) y1 = Pts[i][1];
    }
    return true;
  }
};

inline ddPolygon *RenderBlock::EmplacePolygon(const ddP *pts, int n, uint16_t color, bool closed, bool filled) {
  ddP *copy = A->NewArray<ddP>(n);
  if (!copy) return nullptr;
  for (int i = 0; i < n; i++) new (&copy[i]) ddP(pts[i]);
  ddPolygon *pg = Emplace<ddPolygon>(copy, n, color, closed, filled);
  if (!pg) { A->Delete(copy); return nullptr; }
  pg->Owner = A;
  return pg;
}

// Regular polygon: no point storage, vertices are generated on the fly.
struct ddNGon : ddVectorPrimitive {
  int   Sides;
  float Radius;   // circumradius, world units
  float Phase;    // radians, angle of the first vertex
  bool  Filled;
  ddNGon(ddP c, int sides, float radius, uint16_t color = TFT_WHITE, bool filled = false, float phase = 0.0f)
    : Sides(sides < 3 ? 3 : sides), Radius(radius), Phase(phase), Filled(filled) { Color = color; SetPos(c); }
 protected:
  void Shapes(const Matrix3x3 &m, ddShapeSet &out) override {
    out.BeginPoly(true, Filled);
    for (int i = 0; i < Sides; i++) out.PolyPush(m * Vertex(i));
    out.EndPoly();
  }
  ddP Vertex(int i) const {
    float a = Phase + 2.0f * DDD_PI * i / Sides;
    return ddP(Radius * cosf(a), Radius * sinf(a));
  }
  void DrawShape(const Matrix3x3 &m, ddR *r) override {
    Matrix3x3 f = r->W2D * m;
    r->Tmp.clear();
    for (int i = 0; i < Sides; i++) r->Tmp.push_back(f * Vertex(i));
    r->DrawPolyScreen(r->Tmp.data(), Sides, true, Filled, Color, Pen);
  }
  bool HitLocal(const ddP &p, float tol) override {
    if (Filled) {
      bool in = false;   // even-odd without storing vertices
      for (int i = 0, j = Sides - 1; i < Sides; j = i++) {
        ddP a = Vertex(i), b = Vertex(j);
        if (((a[1] > p[1]) != (b[1] > p[1])) && (p[0] < (b[0]-a[0]) * (p[1]-a[1]) / (b[1]-a[1]) + a[0])) in = !in;
      }
      if (in) return true;
    }
    for (int i = 0, j = Sides - 1; i < Sides; j = i++)
      if (ddSegDist(p, Vertex(j), Vertex(i)) <= tol) return true;
    return false;
  }
  bool LocalBounds(float &x0, float &y0, float &x1, float &y1) override { x0 = y0 = -Radius; x1 = y1 = Radius; return true; }
};

struct ddArrow : ddVectorPrimitive {
  ddP   A, B;                    // tail and tip
  float HeadLength, HeadWidth;   // world units
  bool  Filled;                  // solid head
  ddArrow(ddP a, ddP b, uint16_t color = TFT_WHITE, float headLength = 6.0f, float headWidth = 5.0f, bool filled = true)
    : A(a), B(b), HeadLength(headLength), HeadWidth(headWidth), Filled(filled) { Color = color; }
 protected:
  void Shapes(const Matrix3x3 &m, ddShapeSet &out) override {
    float dx = B[0] - A[0], dy = B[1] - A[1], len = hypotf(dx, dy);
    if (len < 1e-6f) return;
    float ux = dx / len, uy = dy / len, hl = HeadLength < len ? HeadLength : len;
    ddP base(B[0] - ux * hl, B[1] - uy * hl);
    ddP lt(base[0] - uy * HeadWidth * 0.5f, base[1] + ux * HeadWidth * 0.5f);
    ddP rt(base[0] + uy * HeadWidth * 0.5f, base[1] - ux * HeadWidth * 0.5f);
    out.AddSegment(m * A, m * (Filled ? base : B));
    if (Filled) { out.BeginPoly(true, true); out.PolyPush(m * B); out.PolyPush(m * lt); out.PolyPush(m * rt); out.EndPoly(); }
    else        { out.BeginPoly(false, false); out.PolyPush(m * lt); out.PolyPush(m * B); out.PolyPush(m * rt); out.EndPoly(); }
  }
  void DrawShape(const Matrix3x3 &m, ddR *r) override {
    float dx = B[0] - A[0], dy = B[1] - A[1], len = hypotf(dx, dy);
    if (len < 1e-6f) return;
    float ux = dx / len, uy = dy / len;
    float hl = HeadLength < len ? HeadLength : len;
    ddP base(B[0] - ux * hl, B[1] - uy * hl);
    ddP lt(base[0] - uy * HeadWidth * 0.5f, base[1] + ux * HeadWidth * 0.5f);
    ddP rt(base[0] + uy * HeadWidth * 0.5f, base[1] - ux * HeadWidth * 0.5f);
    Matrix3x3 f = r->W2D * m;
    r->DrawLine(f * A, f * (Filled ? base : B), Color, false, Pen);
    r->Tmp.clear();
    if (Filled) { r->Tmp.push_back(f * B); r->Tmp.push_back(f * lt); r->Tmp.push_back(f * rt); }
    else        { r->Tmp.push_back(f * lt); r->Tmp.push_back(f * B); r->Tmp.push_back(f * rt); }
    r->DrawPolyScreen(r->Tmp.data(), 3, false, Filled, Color, Pen);
  }
  bool HitLocal(const ddP &p, float tol) override { return ddSegDist(p, A, B) <= tol; }
  bool LocalBounds(float &x0, float &y0, float &x1, float &y1) override {
    float m = fmaxf(HeadWidth, HeadLength);
    x0 = fminf(A[0], B[0]) - m; x1 = fmaxf(A[0], B[0]) + m; y0 = fminf(A[1], B[1]) - m; y1 = fmaxf(A[1], B[1]) + m;
    return true;
  }
};

// Fixed pixel-size marker at the local origin (size does not scale with the world).
enum struct ddMarkerType { Plus, Cross, Diamond, Square, Circle };
struct ddMarker : ddVectorPrimitive {
  ddMarkerType Type;
  int  SizePx;      // half-size in pixels
  bool Filled;
  ddMarker(ddP p, ddMarkerType type = ddMarkerType::Plus, int sizePx = 4, uint16_t color = TFT_WHITE, bool filled = false)
    : Type(type), SizePx(sizePx), Filled(filled) { Color = color; SetPos(p); }
 protected:
  void Shapes(const Matrix3x3 &m, ddShapeSet &out) override { out.AddPoint(m * ddP(0, 0)); }   // pixel-sized: treated as a point
  void DrawShape(const Matrix3x3 &m, ddR *r) override {
    ddP c = (r->W2D * m) * ddP(0, 0);
    int x = lroundf(c[0]), y = lroundf(c[1]), s = SizePx;
    LGFX_Sprite *g = r->Sprite;
    switch (Type) {
      case ddMarkerType::Plus:
        r->DrawLine(ddP((float)(x - s), (float)y), ddP((float)(x + s), (float)y), Color, false, Pen);
        r->DrawLine(ddP((float)x, (float)(y - s)), ddP((float)x, (float)(y + s)), Color, false, Pen);
        break;
      case ddMarkerType::Cross:
        r->DrawLine(ddP((float)(x - s), (float)(y - s)), ddP((float)(x + s), (float)(y + s)), Color, false, Pen);
        r->DrawLine(ddP((float)(x - s), (float)(y + s)), ddP((float)(x + s), (float)(y - s)), Color, false, Pen);
        break;
      case ddMarkerType::Square:
        if (Filled) g->fillRect(x - s, y - s, 2*s + 1, 2*s + 1, Color); else g->drawRect(x - s, y - s, 2*s + 1, 2*s + 1, Color);
        break;
      case ddMarkerType::Circle:
        if (Filled) g->fillCircle(x, y, s, Color); else g->drawCircle(x, y, s, Color);
        break;
      case ddMarkerType::Diamond: {
        ddP d[4] = { ddP((float)x, (float)(y - s)), ddP((float)(x + s), (float)y), ddP((float)x, (float)(y + s)), ddP((float)(x - s), (float)y) };
        r->DrawPolyScreen(d, 4, true, Filled, Color, Pen);
        break;
      }
    }
  }
  bool HitLocal(const ddP &p, float tol) override { return hypotf(p[0], p[1]) <= tol; }
};

// Circular arc, angles in radians. Sweep runs from StartAngle to EndAngle (negative = clockwise).
// Pie closes it back through the centre (and can then be Filled).
struct ddArc : ddVectorPrimitive {
  float Radius, StartAngle, EndAngle;
  bool  Pie, Filled;
  ddArc(ddP c, float radius, float startAngle, float endAngle, uint16_t color = TFT_WHITE, bool pie = false, bool filled = false)
    : Radius(radius), StartAngle(startAngle), EndAngle(endAngle), Pie(pie), Filled(filled) { Color = color; SetPos(c); }
 protected:
  void Shapes(const Matrix3x3 &m, ddShapeSet &out) override {
    float sweep = EndAngle - StartAngle;
    int steps = (int)(fabsf(sweep) / (DDD_PI / 24.0f)) + 2;
    out.BeginPoly(Pie, Pie && Filled);
    for (int i = 0; i <= steps; i++) { float a = StartAngle + sweep * i / steps; out.PolyPush(m * ddP(Radius * cosf(a), Radius * sinf(a))); }
    if (Pie) out.PolyPush(m * ddP(0, 0));
    out.EndPoly();
  }
  void DrawShape(const Matrix3x3 &m, ddR *r) override {
    float sweep = EndAngle - StartAngle;
    Matrix3x3 f = r->W2D * m;
    float px = Radius * ddMeanScale(m) * r->PixelsPerUnit();
    int steps = ddAutoSteps(fabsf(sweep) * px, 4, 180);
    r->Tmp.clear();
    for (int i = 0; i <= steps; i++) {
      float a = StartAngle + sweep * i / steps;
      r->Tmp.push_back(f * ddP(Radius * cosf(a), Radius * sinf(a)));
    }
    if (Pie) r->Tmp.push_back(f * ddP(0, 0));
    r->DrawPolyScreen(r->Tmp.data(), (int)r->Tmp.size(), Pie, Pie && Filled, Color, Pen);
  }
  bool HitLocal(const ddP &p, float tol) override {
    float d = hypotf(p[0], p[1]);
    float sweep = EndAngle - StartAngle;
    float rel = (sweep >= 0.0f) ? atan2f(p[1], p[0]) - StartAngle : StartAngle - atan2f(p[1], p[0]);
    float span = fabsf(sweep);
    rel = fmodf(rel, 2.0f * DDD_PI); if (rel < 0.0f) rel += 2.0f * DDD_PI;
    bool inside = rel <= span;
    if (Pie && Filled) return inside && d <= Radius + tol;
    return inside && fabsf(d - Radius) <= tol;
  }
  bool LocalBounds(float &x0, float &y0, float &x1, float &y1) override { x0 = y0 = -Radius; x1 = y1 = Radius; return true; }
};

// Quadratic (3 control points) or cubic (4) Bezier curve, flattened to a polyline whose
// segment count follows the on-screen size of the control polygon.
struct ddBezier : ddVectorPrimitive {
  ddP P[4];
  int Count;   // 3 or 4
  ddBezier(ddP p0, ddP p1, ddP p2, uint16_t color = TFT_WHITE) : Count(3) { P[0] = p0; P[1] = p1; P[2] = p2; Color = color; }
  ddBezier(ddP p0, ddP p1, ddP p2, ddP p3, uint16_t color = TFT_WHITE) : Count(4) { P[0] = p0; P[1] = p1; P[2] = p2; P[3] = p3; Color = color; }

  ddP Eval(float t) const {
    float u = 1.0f - t;
    if (Count == 3) {
      float a = u*u, b = 2.0f*u*t, c = t*t;
      return ddP(a*P[0][0] + b*P[1][0] + c*P[2][0], a*P[0][1] + b*P[1][1] + c*P[2][1]);
    }
    float a = u*u*u, b = 3.0f*u*u*t, c = 3.0f*u*t*t, d = t*t*t;
    return ddP(a*P[0][0] + b*P[1][0] + c*P[2][0] + d*P[3][0], a*P[0][1] + b*P[1][1] + c*P[2][1] + d*P[3][1]);
  }
 protected:
  void Shapes(const Matrix3x3 &m, ddShapeSet &out) override {
    out.BeginPoly(false, false);
    for (int i = 0; i <= 24; i++) out.PolyPush(m * Eval(i / 24.0f));
    out.EndPoly();
  }
  void DrawShape(const Matrix3x3 &m, ddR *r) override {
    Matrix3x3 f = r->W2D * m;
    float len = 0.0f;
    for (int i = 0; i + 1 < Count; i++) len += (f * P[i]).DistanceTo(f * P[i + 1]);
    int steps = ddAutoSteps(len, 4, 64);
    r->Tmp.clear();
    for (int i = 0; i <= steps; i++) r->Tmp.push_back(f * Eval((float)i / steps));
    r->DrawPolyScreen(r->Tmp.data(), steps + 1, false, false, Color, Pen);
  }
  bool HitLocal(const ddP &p, float tol) override {
    ddP prev = Eval(0.0f);
    for (int i = 1; i <= 24; i++) {
      ddP next = Eval(i / 24.0f);
      if (ddSegDist(p, prev, next) <= tol) return true;
      prev = next;
    }
    return false;
  }
  bool LocalBounds(float &x0, float &y0, float &x1, float &y1) override {   // convex hull property
    x0 = x1 = P[0][0]; y0 = y1 = P[0][1];
    for (int i = 1; i < Count; i++) {
      if (P[i][0] < x0) x0 = P[i][0]; if (P[i][0] > x1) x1 = P[i][0];
      if (P[i][1] < y0) y0 = P[i][1]; if (P[i][1] > y1) y1 = P[i][1];
    }
    return true;
  }
};

// Image drawn through the primitive's transform (LovyanGFX pushRotateZoom: rotation and
// scale are honoured, shear is not). The local origin is the image pivot (centre by default).
struct ddSprite : ddVectorPrimitive {
  LGFX_Sprite *Src;           // not owned
  float UnitsPerPixel;        // world units per image pixel
  float PivotX, PivotY;       // pivot in image pixels
  bool  HasTransparent = false;
  uint16_t Transparent = 0;
  ddSprite(ddP p, LGFX_Sprite *src, float unitsPerPixel = 1.0f) : Src(src), UnitsPerPixel(unitsPerPixel) {
    PivotX = src ? src->width() * 0.5f : 0.0f;
    PivotY = src ? src->height() * 0.5f : 0.0f;
    SetPos(p);
  }
 protected:
  void Shapes(const Matrix3x3 &m, ddShapeSet &out) override {
    float x0, y0, x1, y1;
    if (!LocalBounds(x0, y0, x1, y1)) return;
    out.BeginPoly(true, true);
    out.PolyPush(m * ddP(x0, y0)); out.PolyPush(m * ddP(x1, y0)); out.PolyPush(m * ddP(x1, y1)); out.PolyPush(m * ddP(x0, y1));
    out.EndPoly();
  }
  void DrawShape(const Matrix3x3 &m, ddR *r) override {
    if (!Src) return;
    // image space (y down, relative to the pivot) -> display
    Matrix3x3 g = (r->W2D * m) * Matrix3x3(Matrix3x3::TransformType::SCALE, UnitsPerPixel, -UnitsPerPixel);
    float zx = hypotf(g.M[0][0], g.M[1][0]), zy = hypotf(g.M[0][1], g.M[1][1]);
    if (zx <= 0.0f || zy <= 0.0f) return;
    float angle = DDD_RAD2DEG(atan2f(g.M[1][0], g.M[0][0]));
    Src->setPivot(PivotX, PivotY);
    if (HasTransparent) Src->pushRotateZoom(r->Sprite, g.M[0][2], g.M[1][2], angle, zx, zy, Transparent);
    else                Src->pushRotateZoom(r->Sprite, g.M[0][2], g.M[1][2], angle, zx, zy);
  }
  bool LocalBounds(float &x0, float &y0, float &x1, float &y1) override {
    if (!Src) return false;
    x0 = -PivotX * UnitsPerPixel;                 x1 = (Src->width()  - PivotX) * UnitsPerPixel;
    y0 = -(Src->height() - PivotY) * UnitsPerPixel; y1 = PivotY * UnitsPerPixel;
    return true;
  }
  bool HitLocal(const ddP &p, float tol) override {
    float x0, y0, x1, y1;
    if (!LocalBounds(x0, y0, x1, y1)) return false;
    return p[0] >= x0 - tol && p[0] <= x1 + tol && p[1] >= y0 - tol && p[1] <= y1 + tol;
  }
};

struct ddText : ddVectorPrimitive {
  char Text[64];
  ddText(ddP p, const char *text, uint16_t color = TFT_WHITE) {
    Color = color; SetPos(p);
    SetText(text);
  }
  void SetText(const char *text) { snprintf(Text, sizeof(Text), "%s", text); }
 protected:
  void DrawShape(const Matrix3x3 &m, ddR *r) override {
    r->DrawCenterString((r->W2D * m) * ddP(0, 0), Text, Color, false);
  }
};

// Grid with optional axes, in local space: lines every `Spacing`, out to +/-HalfW and +/-HalfH.
struct ddGrid : ddVectorPrimitive {
  float Spacing, HalfW, HalfH;
  uint16_t AxisColor;
  bool ShowAxes = true;
  ddGrid(float spacing, float halfW, float halfH, uint16_t color = 0x2104, uint16_t axisColor = TFT_DARKGREY)
    : Spacing(spacing), HalfW(halfW), HalfH(halfH), AxisColor(axisColor) { Color = color; }
 protected:
  void DrawShape(const Matrix3x3 &m, ddR *r) override {
    if (Spacing <= 0.0f) return;
    Matrix3x3 f = r->W2D * m;
    int nx = (int)(HalfW / Spacing), ny = (int)(HalfH / Spacing);
    if (nx > 200) nx = 200;
    if (ny > 200) ny = 200;
    for (int i = -nx; i <= nx; i++) { if (i == 0 && ShowAxes) continue; float x = i * Spacing; r->DrawLine(f * ddP(x, -HalfH), f * ddP(x, HalfH), Color, false); }
    for (int j = -ny; j <= ny; j++) { if (j == 0 && ShowAxes) continue; float y = j * Spacing; r->DrawLine(f * ddP(-HalfW, y), f * ddP(HalfW, y), Color, false); }
    if (ShowAxes) {
      r->DrawLine(f * ddP(0, -HalfH), f * ddP(0, HalfH), AxisColor, false);
      r->DrawLine(f * ddP(-HalfW, 0), f * ddP(HalfW, 0), AxisColor, false);
    }
  }
  bool LocalBounds(float &x0, float &y0, float &x1, float &y1) override { x0 = -HalfW; x1 = HalfW; y0 = -HalfH; y1 = HalfH; return true; }
};

// Group: its own transform applied to every child (children share the parent's arena).
class ddGroup : public ddVectorPrimitive {
 public:
  RenderBlock Children;
  explicit ddGroup(ddArena *a) : Children(a) {}
  ~ddGroup() { Children.Clear(); }
 protected:
  void Shapes(const Matrix3x3 &m, ddShapeSet &out) override {
    for (ddVectorPrimitive *c = Children.First(); c; c = Children.Next(c)) c->CollectShapes(m, out);
  }
  void DrawShape(const Matrix3x3 &m, ddR *r) override { Children.Draw(r, m); }
  bool HitLocal(const ddP &p, float tol) override { return Children.Pick(p, tol) != nullptr; }
  bool LocalBounds(float &x0, float &y0, float &x1, float &y1) override { return Children.Bounds(x0, y0, x1, y1); }
};

inline ddGroup *RenderBlock::EmplaceGroup() { return Emplace<ddGroup>(A); }
