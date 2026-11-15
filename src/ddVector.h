#pragma once
/**
 * @file ddVector.h
 * Scene primitives and the scene container (RenderBlock). This is the heart of the library.
 *
 * A scene is a RenderBlock holding ddVectorPrimitive objects (circles, lines, polygons, groups ...) that live in an arena.
 * You create them with `scene.Emplace<ddCircle>(...)`, keep the returned pointer, change their properties whenever you like
 * (transform, colour, shape fields, visibility) and call `scene.Draw(r)` once per frame.
 *
 * Every primitive has:
 *   - its own transform (SetPos / SetAngle / SetScale / Translate / Transform());
 *   - a Visible flag (a hidden primitive costs nothing: no transform, no culling test, no draw);
 *   - automatic culling: primitives with known bounds that are off-screen are skipped;
 *   - a Pen (width / dashed / dotted) for line-based shapes;
 *   - shape-specific public fields you can change at any time;
 *   - HitTest() for picking, CollectShapes() for collision, plus Draggable / Tag for the interaction layer (ddInteract.h).
 *
 * @par Geometry and position
 *   Geometry is defined around the primitive's LOCAL origin and positioned by its transform. Constructors that take a
 *   centre or position (ddCircle(c, r) ...) place the primitive for you by setting its position, so rotating or scaling it
 *   later pivots around that centre, not around the world origin.
 *
 * @par Lifetime
 *   Primitives live in a ddArena. NEVER `delete` one. Use RenderBlock::Remove / Clear / Reset. A pointer you hold is valid
 *   until you Remove that primitive or Clear/Reset its scene.
 *
 * @warning A primitive class must derive SINGLY from ddVectorPrimitive (no multiple inheritance), so that the object's
 *          address equals the arena block's address.
 * @note To add your own primitive: derive from ddVectorPrimitive, implement DrawShape (and optionally HitLocal,
 *       LocalBounds, Shapes), and create it with `scene.Emplace<YourType>(...)`.
 */
#include <memory>
#include "ddArena.h"
#include "ddRender.h"
#include "ddGeom.h"
#include "ddShapeSet.h"

// ---------------------------------------------------------------- helpers
/**
 * The "average" scale of a matrix: sqrt(|determinant of its 2x2 block|). For a uniform scale it is that scale exactly.
 * @return Scale factor >= 0. Used to convert a world radius into a size under a transform.
 * @warning For a strongly non-uniform scale it is only an approximation (the geometric mean of the two axis scales).
 */
inline float ddMeanScale(const Matrix3x3 &m) {
  return sqrtf(fabsf(m.M[0][0]*m.M[1][1] - m.M[0][1]*m.M[1][0]));
}
/**
 * Distance from a point to a segment. Thin wrapper over ddDistPointLine (kept for the shapes' hit tests).
 */
inline float ddSegDist(const ddP &p, const ddP &a, const ddP &b) { return ddDistPointLine(p, a, b); }
/**
 * Even-odd point-in-polygon. Thin wrapper over ddPointInPolygon (kept for the shapes' hit tests).
 */
inline bool  ddInPoly(const ddP &p, const ddP *v, int n) { return ddPointInPolygon(p, v, n); }
/**
 * Chooses how many segments to use for a curve from its on-screen length: about one segment per 4 pixels, clamped.
 * @param pxLength  Approximate on-screen length of the curve in pixels.
 * @param lo,hi     Clamp range for the result (defaults 12..180).
 * @return Segment count. Used by circles, arcs and Beziers so small curves are cheap and large ones are smooth.
 */
inline int ddAutoSteps(float pxLength, int lo = 12, int hi = 180) {
  int s = (int)(pxLength / 4.0f);
  return s < lo ? lo : (s > hi ? hi : s);
}
/**
 * Axis-aligned bounding box of a box pushed through a matrix (transform the four corners, take min/max).
 * @param m              The transform.
 * @param x0,y0,x1,y1    The input box.
 * @param ox0,oy0,ox1,oy1  Output: the world-space box. Grows when m rotates (a rotated box's AABB is bigger).
 */
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

/**
 * Is the matrix a similarity transform: uniform scale combined with any rotation or reflection (no skew, no non-uniform scale)?
 * @param s  Output: the uniform scale factor (valid when the result is true).
 * @return true if circles stay circles under this matrix.
 */
inline bool ddIsSimilarity(const Matrix3x3 &m, float &s) {
  float sx = hypotf(m.M[0][0], m.M[1][0]), sy = hypotf(m.M[0][1], m.M[1][1]);
  float dot = m.M[0][0]*m.M[0][1] + m.M[1][0]*m.M[1][1];
  s = sx;
  return fabsf(sx - sy) < 1e-4f * (sx + sy) && fabsf(dot) < 1e-4f * sx * sy;
}
/**
 * Adds the unit circle pushed through matrix m to a collision shape set.
 * A true circle part when m is a similarity; otherwise (non-uniform scale / skew produces an ellipse) a closed polygon approximation.
 * @param filled  Disc (true) or rim only (false).
 * @param steps   Polygon segments for the ellipse case (default 48).
 * @note Collision against a non-uniformly scaled circle is therefore an approximation accurate to roughly 1 - cos(PI/48) of the radius.
 */
inline void ddAddUnitCircleShape(const Matrix3x3 &m, ddShapeSet &out, bool filled, int steps = 48) {
  float s;
  if (ddIsSimilarity(m, s)) { out.AddCircle(m * ddP(0, 0), s, filled); return; }
  out.BeginPoly(true, filled);
  for (int i = 0; i < steps; i++) { float a = 2.0f * DDD_PI * i / steps; out.PolyPush(m * ddP(cosf(a), sinf(a))); }
  out.EndPoly();
}

class RenderBlock;

// ---------------------------------------------------------------- base
/**
 * Base class of everything you can put in a scene.
 *
 * Holds the state common to all primitives: colour, visibility, a pen, an application Tag, a Draggable flag and the
 * transform. Provides the public operations (Render, HitTest, CollectShapes, ParentBounds) in terms of virtual hooks that
 * each shape implements: DrawShape, HitLocal, LocalBounds, Shapes.
 *
 * @par Public fields
 *   - `Color`     RGB565 colour (TFT_* constants or lcd.color565()). Default white.
 *   - `Visible`   false = completely skipped by Draw, HitTest/Pick and collision. Use it instead of Remove for things that come back.
 *   - `Draggable` true = ddInteract moves it when dragged.
 *   - `Tag`       Free for your use (button ids, object kinds ...). Not interpreted by the library.
 *   - `Pen`       Width / style of line-based shapes (see ddPen).
 *
 * @warning Do not store a primitive by value or copy one: it is arena-allocated and linked into a list.
 * @warning Changing a transform through SetPos/SetAngle/SetScale/Translate is cheap and lazy; the matrix is rebuilt once at the
 *          next Render / HitTest / bounds query.
 */
struct ddVectorPrimitive {
  uint16_t Color = TFT_WHITE;
  bool     Visible = true;
  bool     Draggable = false;   // used by ddInteract
  uint16_t Tag = 0;             // free for the application (e.g. button ids)
  ddPen    Pen;                 // stroke for line-based shapes

  /**
   * Sets the primitive's position (the translation of its transform, in its PARENT's space - world space for top-level primitives).
   * @note The primitive pivots (rotates / scales) about its local origin, which is where it was placed.
   */
  void SetPos(float x, float y)   { Xf.SetTranslation(x, y); Touch(); }
  /**
   * Same as SetPos(x, y) with a point.
   */
  void SetPos(const ddP &p)       { SetPos(p[0], p[1]); }
  /**
   * Sets the rotation about the local origin.
   * @param radians  Counter-clockwise (y up). Use DDD_DEG2RAD for degrees.
   * @note This is ABSOLUTE (it replaces the previous angle). To spin continuously, keep your own angle variable, or read GetTransform().Angle and add to it.
   */
  void SetAngle(float radians)    { Xf.SetRotation(radians); Touch(); }
  /**
   * Sets the scale about the local origin (absolute, replaces the previous scale).
   * @warning Non-uniform scale on a circle turns it into an ellipse (drawn and collided as a polygon). Negative values mirror.
   */
  void SetScale(float x, float y) { Xf.SetScale(x, y); Touch(); }
  /**
   * Uniform scale (same factor on both axes).
   */
  void SetScale(float s)          { SetScale(s, s); }
  /**
   * Moves the primitive by an offset (relative; adds to the current position). Used by dragging.
   */
  void Translate(float dx, float dy) { Xf.SetTranslation(Xf.Tx + dx, Xf.Ty + dy); Touch(); }
  /**
   * Direct access to the transform for bulk changes (Tx, Ty, Angle, Sx, Sy).
   * @warning Calling Transform() marks the transform dirty at that moment. If you keep the reference and modify it later without
   *          calling Transform() again, the change is not picked up until something else marks it dirty. Fetch it each time.
   */
  ddT &Transform()                { Touch(); return Xf; }   // direct access; assumed modified
  /**
   * Read-only access to the transform (position `Tx,Ty`, `Angle`, scale `Sx,Sy`). Does not mark anything dirty.
   * @note The stored components are always current even if the matrix `M` has not been rebuilt yet.
   */
  const ddT &GetTransform() const { return Xf; }

  /**
   * Draws the primitive (called by RenderBlock::Draw).
   * Hidden primitives do nothing. Otherwise: combine the transform with `parent`, cull if its bounds are off-screen, then DrawShape.
   * @param parent  World-from-parent matrix (identity for top-level primitives; a group passes its own matrix to its children).
   * @param r       The renderer.
   * @note You normally never call this directly.
   */
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

  /**
   * Is a point on/inside this primitive?
   * @param p    The point in the PARENT's space (world space for top-level primitives; ddGroup tests its children this way).
   * @param tol  Slop in the same units, e.g. a few pixels converted with 1 / PixelsPerUnit(). Default 0.
   * @return true on a hit. Filled shapes hit anywhere inside; unfilled shapes only near their outline.
   * @warning Hidden primitives are NOT excluded here (RenderBlock::Pick skips them). Check Visible yourself if you call HitTest directly.
   */
  bool HitTest(const ddP &p, float tol = 0.0f) {
    if (!UsesXf) return HitLocal(p, tol);
    if (Dirty) { Xf.Combine(); Dirty = false; }
    float s = ddMeanScale(Xf.M);
    if (s < 1e-9f) return false;
    Matrix3x3 inv = Xf.M; inv.Invert();
    return HitLocal(inv * p, tol / s);
  }

  /**
   * Appends this primitive's collision geometry, in WORLD space, to a shape set. Hidden primitives add nothing.
   * @param parent  World-from-parent matrix.
   * @param out     The set to append to (not cleared).
   * @note This is what ddOverlap / ddCollide / ddFindOverlaps are built on. Call it yourself to build custom collision queries.
   */
  void CollectShapes(const Matrix3x3 &parent, ddShapeSet &out) {
    if (!Visible) return;
    if (!UsesXf) { Shapes(parent, out); return; }
    if (Dirty) { Xf.Combine(); Dirty = false; }
    Shapes(parent * Xf.M, out);
  }

  /**
   * Axis-aligned bounds of the primitive in its PARENT's space (world space for top-level primitives), including its transform.
   * @param x0,y0,x1,y1  Outputs: min/max corners.
   * @return false if the shape has no known bounds (text, markers) - such shapes are never culled.
   * @note A rotated primitive's AABB is larger than the shape itself; use it for broad-phase rejection only.
   */
  bool ParentBounds(float &x0, float &y0, float &x1, float &y1) {
    float lx0, ly0, lx1, ly1;
    if (!LocalBounds(lx0, ly0, lx1, ly1)) return false;
    if (!UsesXf) { x0 = lx0; y0 = ly0; x1 = lx1; y1 = ly1; return true; }
    if (Dirty) { Xf.Combine(); Dirty = false; }
    ddBoxThrough(Xf.M, lx0, ly0, lx1, ly1, x0, y0, x1, y1);
    return true;
  }

 protected:
  /**
   * Marks the transform as in use and needing a rebuild. Called by every setter.
   */
  void Touch() { UsesXf = true; Dirty = true; }
  /**
   * Hook: draw the shape. Implemented by every primitive.
   * @param m  World-from-local matrix (parent chain and this primitive's transform already combined).
   * @param r  The renderer (use r->W2D * m to go from local coordinates straight to display pixels).
   */
  virtual void DrawShape(const Matrix3x3 &m, ddR *r) = 0;                 // m = world-from-local
  /**
   * Hook: hit test in the primitive's LOCAL space. The base class converts the incoming point and tolerance
   * (dividing the tolerance by the mean scale). Default: never hit.
   */
  virtual bool HitLocal(const ddP &, float) { return false; }              // point in local space
  /**
   * Hook: add collision geometry in WORLD space. Default: none (the primitive does not collide).
   * @note Circles that stay circles, polygons, polylines, segments and points are produced; see ddShapeSet.h.
   */
  virtual void Shapes(const Matrix3x3 &, ddShapeSet &) {}                  // collision geometry; m = world-from-local
  /**
   * Hook: local-space bounding box, used for culling and ParentBounds. Return false for "unknown / unbounded" (never culled).
   */
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

/**
 * The scene: an ordered list of primitives backed by an arena. Draw order is insertion order (the last added is on top).
 *
 * A top-level RenderBlock OWNS an arena that grows on demand up to a byte cap. A RenderBlock built from an existing arena (this is how
 * ddGroup holds its children) shares it.
 *
 * @code
 * RenderBlock scene;                                   // default cap 256 KB
 * ddCircle *c = scene.Emplace<ddCircle>(ddP(0, 0), 10.0f, TFT_CYAN, true);
 * if (!c) handleFull();                                // arena cap reached
 * c->SetPos(5, 5);
 * scene.Draw(&renderer);
 * scene.Remove(c);                                     // c is invalid from here on
 * scene.Clear();                                       // remove everything, keep the memory
 * @endcode
 *
 * Two usage styles are supported and can be mixed:
 *   - RETAINED: build the scene once, then modify objects each frame (cheapest per frame).
 *   - PER-FRAME: `scene.Clear()` then re-Emplace everything each frame. Clear() on an owning block keeps its chunks, so
 *     this does no malloc/free.
 *
 * @warning Emplace returns nullptr when the arena cap is reached. Check it (or size the cap generously and watch Arena()->Overflowed()).
 * @warning Not thread-safe.
 * @warning After Remove(p), Clear() or Reset(), every pointer to the affected primitives is dangling. If you also keep a copy in an
 *          interaction layer or your own list, drop it first (ddInteract::Forget).
 */
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
  /**
   * Creates a scene that owns its own arena.
   * @param maxBytes  Byte cap for the arena (default DD_ARENA_DEFAULT_MAX = 256 KB). Size it from Arena()->Peak() of your heaviest scene.
   */
  explicit RenderBlock(size_t maxBytes = DD_ARENA_DEFAULT_MAX) : Own(new ddArena(maxBytes)) { A = Own.get(); }
  /**
   * Creates a block that allocates from an existing arena (used by ddGroup). Clear() on such a block destroys its primitives
   * individually instead of resetting the whole arena.
   */
  explicit RenderBlock(ddArena *shared) : A(shared) {}
  /**
   * Non-copyable.
   */
  RenderBlock(const RenderBlock &) = delete;
  RenderBlock &operator=(const RenderBlock &) = delete;

  /**
   * @return The arena in use (for statistics: InUse, Peak, Capacity, Overflowed).
   */
  ddArena *Arena() { return A; }

  /**
   * Creates a primitive in the arena and adds it on top of the draw order.
   * @tparam T     A class derived from ddVectorPrimitive (ddCircle, ddRect ... or your own).
   * @param args   The constructor arguments of T.
   * @return       Pointer to the new primitive, or nullptr if the arena cap was reached.
   * @warning Check for nullptr. Keep the pointer if you want to modify, hide or remove the primitive later.
   */
  template <class T, class... Args>
  T *Emplace(Args &&...args) {
    T *p = A->New<T>(std::forward<Args>(args)...);
    if (p) Link(p);
    return p;
  }
  /**
   * Creates a polygon or polyline whose points are COPIED into the arena, so your source array can be a temporary.
   * @param pts     Vertices in local coordinates.
   * @param n       Vertex count.
   * @param closed  true = polygon (last vertex connects to the first), false = open polyline.
   * @param filled  true = solid (needs `closed`).
   * @return        The polygon, or nullptr if the arena is full. The copied points are freed with the polygon.
   * @note Prefer this over constructing a ddPolygon with a pointer you must keep alive yourself.
   */
  ddPolygon *EmplacePolygon(const ddP *pts, int n, uint16_t color = TFT_WHITE, bool closed = true, bool filled = false);
  /**
   * Creates an empty group (a primitive with its own transform that contains child primitives).
   * @return The group, or nullptr if the arena is full. Add children with `group->Children.Emplace<...>(...)`.
   */
  ddGroup   *EmplaceGroup();

  /**
   * Unlinks a primitive from the draw list WITHOUT destroying it. The pointer stays valid and its memory is reclaimed at the next
   * Clear() / Reset(). Rarely needed; Remove() is usually what you want.
   */
  void Detach(ddVectorPrimitive *p) { if (p) Unlink(p); }
  /**
   * Unlinks, destroys and recycles a primitive. For a group this also destroys all its children.
   * @warning `p` is invalid afterwards. Do not call it twice for the same pointer.
   */
  void Remove(ddVectorPrimitive *p) {
    if (!p) return;
    Unlink(p);
    A->Delete(p);
  }
  /**
   * Removes every primitive.
   * An owning block resets its arena (all chunks are kept, so this is cheap and does not free memory to the system). A block that shares an arena
   * destroys and recycles each of its primitives individually.
   */
  void Clear() {
    if (Own) { A->Reset(); Head = Tail = nullptr; N = 0; return; }
    if (A->IsResetting()) { Head = Tail = nullptr; N = 0; return; }   // arena destroys them itself
    for (ddVectorPrimitive *p = Head; p;) { ddVectorPrimitive *nx = p->Next; A->Delete(p); p = nx; }
    Head = Tail = nullptr; N = 0;
  }
  /**
   * Alias for Clear().
   */
  void Reset() { Clear(); }

  /**
   * Moves a primitive to the top of the draw order (drawn last, picked first).
   */
  void MoveToFront(ddVectorPrimitive *p) { if (p && p != Tail) { Unlink(p); Link(p); } }
  /**
   * Moves a primitive to the bottom of the draw order (drawn first, picked last).
   */
  void MoveToBack(ddVectorPrimitive *p) {
    if (!p || p == Head) return;
    Unlink(p);
    p->Next = Head; p->Prev = nullptr;
    if (Head) Head->Prev = p; else Tail = p;
    Head = p; N++;
  }

  /**
   * @return Number of top-level primitives in this block (children of groups are counted by their own blocks).
   */
  int Count() const { return N; }
  /**
   * @return The first (bottom) primitive, or nullptr. Together with Next() lets you iterate the block.
   */
  ddVectorPrimitive *First() const { return Head; }
  /**
   * @return The primitive drawn after p, or nullptr at the end.
   */
  ddVectorPrimitive *Next(const ddVectorPrimitive *p) const { return p->Next; }

  /**
   * Draws every primitive in order, applying transforms and culling.
   * @param r       The renderer.
   * @param parent  World-from-parent matrix (default identity). A ddGroup passes its own transform here for its children.
   * @note Call r->ResetStats() before this if you want per-frame Drawn / Culled counts.
   */
  void Draw(ddR *r, const Matrix3x3 &parent = Matrix3x3()) {
    for (ddVectorPrimitive *p = Head; p; p = p->Next) p->Render(parent, r);
  }

  /**
   * Finds the topmost visible primitive under a point.
   * @param p    The point in world space (e.g. r->ToWorld(touchPixel)).
   * @param tol  Slop in world units (convert pixels with 1 / r->PixelsPerUnit()).
   * @return The primitive, or nullptr. Returns top-level primitives (a hit on a group's child returns the group).
   * @note Iterates from the top of the draw order, so overlapping objects are resolved visually.
   */
  ddVectorPrimitive *Pick(const ddP &p, float tol = 0.0f) {
    for (ddVectorPrimitive *q = Tail; q; q = q->Prev)
      if (q->Visible && q->HitTest(p, tol)) return q;
    return nullptr;
  }

  /**
   * Union of the bounds of the visible primitives, in this block's space.
   * @return false if the block is empty or ANY visible child has unknown bounds.
   */
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
/**
 * A filled dot.
 *
 * @par Fields
 *   - `Radius` in WORLD units (scales with the transform and the world). `Label` draws the world coordinates above it (needs DD_DEBUG).
 *
 * @par Behaviour
 *   Draws as a filled disc. Hit-tests and collides as a filled disc of that radius.
 * @warning A very small radius can round to 0 pixels and vanish; use ddMarker for a fixed pixel-sized mark.
 */
struct ddPoint : ddVectorPrimitive {
  float Radius;     // world units
  bool  Label;      // draw world coordinates above the point (needs DD_DEBUG)
  /**
   * @param p      Position (world units).
   * @param rad    Radius in world units (default 1).
   * @param color  RGB565 colour (default white).
   * @param label  Draw the coordinates label (only with DD_DEBUG).
   */
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

/**
 * A line segment from `A` to `B`, both in local coordinates.
 *
 * @par Fields
 *   - `A`, `B` end points. Use `Pen` for width and dashes.
 *
 * @par Behaviour
 *   Hit-tests near the segment; collides as a segment.
 * @note The constructor does NOT move the primitive: A and B are interpreted in the parent's space until you give it a transform.
 */
struct ddLine : ddVectorPrimitive {
  ddP A, B;
  /**
   * @param a,b   End points.
   * @param color  RGB565 colour (default white).
   */
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

/**
 * Draws the unit circle pushed through a matrix. The shared drawing code of ddCircle and ddEllipse.
 * Uses LovyanGFX's circle/ellipse primitives when the result is axis-aligned on screen and the pen is plain (or the shape is filled); otherwise
 * a polyline whose segment count follows the on-screen size, which also honours the pen.
 * @param m        World-from-unit-circle matrix (already includes the radius scale).
 * @param steps    Fallback segment count; <= 0 means automatic.
 * @param filled   Fill instead of outline.
 */
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

/**
 * A circle (outline or disc).
 *
 * @par Fields
 *   - `Radius` world units. `Filled` disc instead of rim. `Steps` fallback polygon segments (0 = automatic).
 *
 * @par Behaviour
 *   Drawn with LovyanGFX's fast circle when the transform keeps it round and the pen is plain; otherwise as a polyline. Under a NON-UNIFORM
 *   scale it becomes an ellipse. Collides as a true circle when the transform is a similarity, otherwise as a 48-sided polygon.
 * @warning An unfilled circle is only its rim for hit-testing and collision: a point or small shape inside it does not touch it.
 *          Use `Filled = true`, or pass solid = true to the collision functions, to treat it as a disc.
 */
struct ddCircle : ddVectorPrimitive {
  float Radius;   // world units
  bool  Filled;
  int   Steps;    // fallback segment count; 0 = automatic
  /**
   * @param c       Centre (world units); sets the position.
   * @param radius  World units.
   * @param color   RGB565 colour (default white).
   * @param filled  Disc (true) or rim (false, default).
   * @param steps   Fallback segment count (0 = automatic).
   */
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

/**
 * An axis-aligned ellipse in local space; rotate it with SetAngle().
 *
 * @par Fields
 *   - `Rx`, `Ry` radii in world units. `Filled`, `Steps` as for ddCircle.
 *
 * @par Behaviour
 *   Fast LovyanGFX ellipse when axis-aligned on screen with a plain pen; polyline otherwise (always the case once rotated by an angle that is not a multiple of 90 degrees).
 */
struct ddEllipse : ddVectorPrimitive {
  float Rx, Ry;   // world units; rotate with SetAngle()
  bool  Filled;
  int   Steps;
  /**
   * @param c       Centre (world units).
   * @param rx,ry   Radii in world units.
   * @param color   RGB565 colour. @param filled Fill. @param steps Fallback segments (0 = automatic).
   */
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

/**
 * A rectangle centred on its local origin (rotate it with SetAngle).
 *
 * @par Fields
 *   - `W`, `H` width and height in world units. `Filled`.
 *
 * @par Behaviour
 *   Axis-aligned on screen (including 90-degree turns) with a plain pen: LovyanGFX drawRect/fillRect. Otherwise drawn as a 4-point polygon.
 *   Collides as a (rotated) quadrilateral.
 * @warning Positioned by its CENTRE, not its top-left corner.
 */
struct ddRect : ddVectorPrimitive {
  float W, H;     // world units, centred on the local origin
  bool  Filled;
  /**
   * @param c       Centre (world units).
   * @param w,h     Width and height in world units.
   * @param color   RGB565 colour. @param filled Fill.
   */
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

/**
 * A polygon (closed) or polyline (open) through a list of points in local coordinates.
 *
 * @par Fields
 *   - `Pts`, `Count` the vertices. `Closed` joins the last to the first. `Filled` fills (concave allowed; even-odd). `Owner` is set when the points live in the arena.
 *
 * @par Behaviour
 *   Filled polygons use a scanline fill that handles concave outlines. Hit-tests inside (when filled) or near the edges. Collides as a polygon / polyline.
 *
 * @warning `Pts` is NOT copied by the constructor. The array must outlive the primitive. Use RenderBlock::EmplacePolygon to have it copied into the arena instead.
 * @warning Bounds are recomputed from the points every frame (O(n)); very large polygons that never change are better kept as a few shapes.
 * @note You may edit the points in place between frames; the polygon reads them each draw.
 */
struct ddPolygon : ddVectorPrimitive {
  const ddP *Pts;
  int  Count;
  bool Closed, Filled;
  ddArena *Owner = nullptr;   // set when Pts lives in the arena
  /**
   * @param pts     Vertices (not copied).
   * @param n       Vertex count.
   * @param color   RGB565 colour. @param closed true = polygon. @param filled true = solid.
   */
  ddPolygon(const ddP *pts, int n, uint16_t color = TFT_WHITE, bool closed = true, bool filled = false)
    : Pts(pts), Count(n), Closed(closed), Filled(filled) { Color = color; }
  /**
   * Frees the point array if it was allocated by EmplacePolygon (Owner set).
   */
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

/**
 * A regular polygon (triangle, square, hexagon ...) with no point storage; vertices are generated on the fly.
 *
 * @par Fields
 *   - `Sides` (>= 3), `Radius` circumradius in world units, `Phase` angle of the first vertex in radians, `Filled`.
 *
 * @par Behaviour
 *   Drawn as a polygon; collides as a polygon. Cheap in memory (a few floats).
 */
struct ddNGon : ddVectorPrimitive {
  int   Sides;
  float Radius;   // circumradius, world units
  float Phase;    // radians, angle of the first vertex
  bool  Filled;
  /**
   * @param c       Centre (world units).
   * @param sides   Number of sides (values below 3 are raised to 3).
   * @param radius  Circumradius in world units.
   * @param color   RGB565 colour. @param filled Fill. @param phase Rotation of the first vertex in radians.
   */
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

/**
 * An arrow: a shaft from `A` (tail) to `B` (tip) with a head at B.
 *
 * @par Fields
 *   - `A`, `B` local end points. `HeadLength`, `HeadWidth` in world units. `Filled` = solid triangular head (shaft stops at its base); false = open "V" head.
 *
 * @par Behaviour
 *   The head is built in local space so it follows the transform. Hit-tests along the shaft. Collides as the shaft plus the head.
 * @warning A zero-length arrow draws nothing. The head length is clamped to the arrow's length.
 */
struct ddArrow : ddVectorPrimitive {
  ddP   A, B;                    // tail and tip
  float HeadLength, HeadWidth;   // world units
  bool  Filled;                  // solid head
  /**
   * @param a,b         Tail and tip.
   * @param color        RGB565 colour. @param headLength World units along the shaft. @param headWidth World units across. @param filled Solid head.
   */
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

/**
 * Shape of a ddMarker: a plus, an X, a diamond, a square or a circle.
 */
enum struct ddMarkerType { Plus, Cross, Diamond, Square, Circle };
/**
 * A fixed PIXEL-size mark at its local origin: it does not grow when the world is scaled. Good for plotted data points and cursors.
 *
 * @par Fields
 *   - `Type` (ddMarkerType), `SizePx` half-size in pixels, `Filled` for the closed shapes.
 *
 * @par Behaviour
 *   Has no bounds, so it is never culled. Hit-tests only within the tolerance of its position, and collides as a single point (its pixel size is
 *   unknown to the world-space code).
 * @warning Because its bounds are unknown, thousands of markers are drawn even when off-screen. Hide or remove those you do not need.
 */
struct ddMarker : ddVectorPrimitive {
  ddMarkerType Type;
  int  SizePx;      // half-size in pixels
  bool Filled;
  /**
   * @param p        Position (world units).
   * @param type     Marker shape (default Plus).
   * @param sizePx   Half-size in pixels (default 4).
   * @param color    RGB565 colour. @param filled Fill (Diamond, Square, Circle).
   */
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

/**
 * A circular arc, optionally closed through the centre as a pie slice.
 *
 * @par Fields
 *   - `Radius` world units. `StartAngle`, `EndAngle` radians; the sweep runs from start to end (end < start = clockwise).
 *   - `Pie` joins the ends through the centre. `Filled` fills a Pie (concave pies are fine).
 *
 * @par Behaviour
 *   Drawn and collided as a polyline whose segment count follows the on-screen size.
 * @warning `Filled` only has an effect when `Pie` is true.
 * @note Animate a gauge by changing EndAngle each frame.
 */
struct ddArc : ddVectorPrimitive {
  float Radius, StartAngle, EndAngle;
  bool  Pie, Filled;
  /**
   * @param c           Centre (world units).
   * @param radius      World units.
   * @param startAngle,endAngle  Radians.
   * @param color       RGB565 colour. @param pie Close through the centre. @param filled Fill the pie.
   */
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

/**
 * A quadratic (3 control points) or cubic (4) Bezier curve.
 *
 * @par Fields
 *   - `P[4]` the control points (local coordinates), `Count` 3 or 4. Eval(t) gives the point at parameter t in 0..1.
 *
 * @par Behaviour
 *   Flattened to a polyline whose segment count follows the on-screen length of the control polygon (4..64). Bounds are the control points' box
 *   (the curve is always inside it). Collides as a 24-segment polyline.
 */
struct ddBezier : ddVectorPrimitive {
  ddP P[4];
  int Count;   // 3 or 4
  /**
   * Quadratic Bezier. @param p0,p2 End points. @param p1 Control point. @param color RGB565 colour.
   */
  ddBezier(ddP p0, ddP p1, ddP p2, uint16_t color = TFT_WHITE) : Count(3) { P[0] = p0; P[1] = p1; P[2] = p2; Color = color; }
  /**
   * Cubic Bezier. @param p0,p3 End points. @param p1,p2 Control points. @param color RGB565 colour.
   */
  ddBezier(ddP p0, ddP p1, ddP p2, ddP p3, uint16_t color = TFT_WHITE) : Count(4) { P[0] = p0; P[1] = p1; P[2] = p2; P[3] = p3; Color = color; }

  /**
   * Point on the curve.
   * @param t  Parameter, 0 = first control point, 1 = last. Values outside 0..1 extrapolate.
   * @return   The point in local coordinates.
   */
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

/**
 * An image (an LGFX_Sprite) drawn through the primitive's transform: rotation and uniform/axis scale are honoured, shear is not.
 *
 * @par Fields
 *   - `Src` the image (NOT owned: it must outlive the primitive). `UnitsPerPixel` world units per image pixel. `PivotX`, `PivotY` the pivot in image pixels (centre by default; the local origin).
 *   - `HasTransparent` + `Transparent` an RGB565 colour key to skip.
 *
 * @par Behaviour
 *   Drawn with LovyanGFX pushRotateZoom (nearest-neighbour). Hit-tests and collides as its (rotated) rectangle.
 * @warning The image is drawn nearest-neighbour; scaling up shows pixels and rotating can show jaggies.
 * @warning Mirroring (negative scale) is not supported by the underlying call.
 */
struct ddSprite : ddVectorPrimitive {
  LGFX_Sprite *Src;           // not owned
  float UnitsPerPixel;        // world units per image pixel
  float PivotX, PivotY;       // pivot in image pixels
  bool  HasTransparent = false;
  uint16_t Transparent = 0;
  /**
   * @param p              Position of the pivot (world units).
   * @param src            The image. Must stay alive and unchanged in size.
   * @param unitsPerPixel  World units per image pixel (default 1).
   */
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

/**
 * A text label centred on its position, in LovyanGFX's built-in 6x8 Font0.
 *
 * @par Fields
 *   - `Text` up to 63 characters (copied). Change it with SetText().
 *
 * @par Behaviour
 *   Always pixel-sized (it does not scale with the world or the transform's scale; only the position is used). No bounds (never culled) and no collision geometry.
 */
struct ddText : ddVectorPrimitive {
  char Text[64];
  /**
   * @param p     Centre position (world units).
   * @param text  Text (truncated to 63 characters).
   * @param color RGB565 colour.
   */
  ddText(ddP p, const char *text, uint16_t color = TFT_WHITE) {
    Color = color; SetPos(p);
    SetText(text);
  }
  /**
   * Replaces the text (truncated to 63 characters, always NUL-terminated).
   * @warning Formatting numbers into the text with snprintf each frame costs time; only update when the value changes.
   */
  void SetText(const char *text) { snprintf(Text, sizeof(Text), "%s", text); }
 protected:
  void DrawShape(const Matrix3x3 &m, ddR *r) override {
    r->DrawCenterString((r->W2D * m) * ddP(0, 0), Text, Color, false);
  }
};

/**
 * A grid of lines with optional axes, in local space: lines every `Spacing` out to +/-`HalfW` and +/-`HalfH`.
 *
 * @par Fields
 *   - `Spacing`, `HalfW`, `HalfH` world units. `Color` the grid lines, `AxisColor` the two axes. `ShowAxes`.
 *
 * @par Behaviour
 *   Useful as a visual reference. At most 200 lines are drawn in each direction. Has bounds (so it is culled when off-screen) but no hit or collision geometry.
 */
struct ddGrid : ddVectorPrimitive {
  float Spacing, HalfW, HalfH;
  uint16_t AxisColor;
  bool ShowAxes = true;
  /**
   * @param spacing    Distance between lines in world units (<= 0 draws nothing).
   * @param halfW,halfH Half extents in world units.
   * @param color       Grid colour (default dark gray). @param axisColor Axis colour.
   */
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

/**
 * A primitive that contains other primitives and gives them a shared transform (a node in a scene graph).
 *
 * @code
 * ddGroup *car = scene.EmplaceGroup();
 * car->Children.Emplace<ddRect>(ddP(0, 0), 20.0f, 8.0f);
 * car->Children.Emplace<ddCircle>(ddP(-7, -4), 3.0f);
 * car->SetPos(50, 0); car->SetAngle(0.3f);        // moves and rotates every child
 * @endcode
 *
 * @par Fields
 *   - `Children` a RenderBlock sharing the parent's arena. Add, remove, pick and iterate children through it.
 *
 * @par Behaviour
 *   Transforms compose down the tree (child transform inside the group's). Groups can nest. Bounds are the union of the visible children's bounds (unbounded if any child is).
 *   Picking a child returns the TOP-LEVEL group. Removing a group destroys all its children.
 * @warning Children have their own Visible flags, but a hidden group hides everything inside it.
 * @note The child pointers stay valid until you remove them or their group.
 */
class ddGroup : public ddVectorPrimitive {
 public:
  RenderBlock Children;
  /**
   * Creates a group whose children use the given arena. Use RenderBlock::EmplaceGroup() instead of calling this directly.
   */
  explicit ddGroup(ddArena *a) : Children(a) {}
  /**
   * Destroys all children (a no-op during an arena reset, which already destroys everything).
   */
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
