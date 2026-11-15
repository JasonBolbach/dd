#pragma once
/**
 * @file ddShapeSet.h
 * World-space collision geometry for shape-to-shape tests, and the pairwise tests on it. Graphics-free.
 *
 * Primitives (ddVector.h) fill a ddShapeSet from their Shapes() method; ddCollide.h wraps it with the primitive-level API.
 * You only need this header directly to build custom collision queries or to test geometry you describe yourself.
 *
 * @par A shape is a set of PARTS
 *   - Point    a single point
 *   - Circle   centre + radius; Filled = a disc, otherwise just the rim
 *   - Segment  one line segment
 *   - Poly     vertices; Closed polygons are solid when Filled, open ones are polylines
 *
 * @par Semantics (the same as how the shapes are drawn)
 *   - an unfilled shape is only its OUTLINE, so a small unfilled circle inside a big one does NOT touch it;
 *   - a filled shape is SOLID, so the same circle inside a filled one DOES overlap it;
 *   - pass solid = true to treat every closed shape as filled (physics-style "do these occupy common space");
 *   - "contacts" are boundary contact points (outline crossings or touches). Pure containment has no boundary contact: use Overlap for that.
 *
 * @warning All tests are O(parts_a x parts_b) with a bounding-box rejection per pair. Shapes with thousands of edges are slow to compare;
 *          simplify them or test against their bounds first.
 * @note Everything is in WORLD space. The primitives apply their transform when they build the set.
 */
#include <vector>
#include "ddGeom.h"

/**
 * World-unit tolerance (1e-4): shapes touching within this distance count as touching, and contact points closer than this are merged.
 */
constexpr float DD_SHAPE_TOL = 1e-4f;       // world units: touching within this counts as touching

/**
 * One piece of a shape: a point, circle, segment or polygon/polyline.
 * @par Fields
 *   - `K` kind; `Filled` solid (circles, closed polys); `Closed` (polys); `A`,`B` centre / end points; `R` circle radius; `First`,`Count` vertex range of a Poly in the set's `Pts`; `Box` bounds.
 * @note Built through ddShapeSet::Add*; you normally only read them.
 */
struct ddShapePart {
  /**
   * Kinds of part: a single Point, a Circle (rim or disc), a line Segment, or a Poly (polygon / polyline).
   */
  enum Kind : uint8_t { Point, Circle, Segment, Poly };
  Kind K = Point;
  bool Filled = false, Closed = false;
  ddP  A, B;                  // Point/Circle: A = centre; Segment: A -> B
  float R = 0.0f;             // Circle radius
  int  First = 0, Count = 0;  // Poly: vertex range in ddShapeSet::Pts
  ddBox Box;                  // bounds of this part
};

/**
 * A list of shape parts with their combined bounds: the world-space collision geometry of one primitive (or any shape you assemble).
 *
 * @code
 * ddShapeSet s;
 * s.AddCircle(ddP(0, 0), 5.0f, true);                     // a disc
 * s.AddSegment(ddP(-10, 0), ddP(10, 0));                  // plus a segment
 * bool hit = ddShapeSetsOverlap(s, other);
 * @endcode
 *
 * @note Reuse one set and call Clear() between uses: the internal vectors keep their capacity, so there is no allocation after warm-up.
 * @warning Uses std::vector (heap). The first fill allocates; keep sets around rather than creating them per call on a microcontroller.
 */
class ddShapeSet {
 public:
  /**
   * The parts, in the order added.
   */
  std::vector<ddShapePart> Parts;
  /**
   * Vertex storage shared by all Poly parts (each Poly refers to a range).
   */
  std::vector<ddP> Pts;
  /**
   * Bounding box of every part (empty for an empty set). Used as the first, cheapest rejection test.
   */
  ddBox Bounds;               // bounds of everything

  /**
   * Removes all parts (capacity is kept).
   */
  void Clear() { Parts.clear(); Pts.clear(); Bounds = ddBox(); }
  /**
   * @return true if there are no parts.
   */
  bool Empty() const { return Parts.empty(); }

  /**
   * Adds a point part.
   */
  void AddPoint(const ddP &p) {
    ddShapePart s; s.K = ddShapePart::Point; s.A = p; s.Box = ddBox(p[0], p[1], p[0], p[1]);
    Push(s);
  }
  /**
   * Adds a circle.
   * @param c       Centre.  @param r Radius (a negative radius adds nothing).
   * @param filled  true = disc, false = rim only.
   */
  void AddCircle(const ddP &c, float r, bool filled) {
    if (r < 0.0f) return;
    ddShapePart s; s.K = ddShapePart::Circle; s.A = c; s.R = r; s.Filled = filled; s.Box = ddBoxOfCircle(c, r);
    Push(s);
  }
  /**
   * Adds a line segment from a to b.
   */
  void AddSegment(const ddP &a, const ddP &b) {
    ddShapePart s; s.K = ddShapePart::Segment; s.A = a; s.B = b; s.Box = ddBox::FromCorners(a, b);
    Push(s);
  }
  /**
   * Starts a polygon / polyline. Follow with PolyPush() for each vertex and EndPoly().
   * @param closed  true = polygon, false = open polyline.
   * @param filled  true = solid (only meaningful when closed).
   * @warning Do not interleave other Add* calls between BeginPoly and EndPoly.
   */
  void BeginPoly(bool closed, bool filled) {
    PolyStart = (int)Pts.size(); PolyClosed = closed; PolyFilled = filled;
  }
  /**
   * Adds a vertex to the polygon started by BeginPoly().
   */
  void PolyPush(const ddP &p) { Pts.push_back(p); }
  /**
   * Finishes the polygon. A single vertex becomes a Point part; zero vertices adds nothing.
   */
  void EndPoly() {
    int n = (int)Pts.size() - PolyStart;
    if (n <= 0) return;
    if (n == 1) { ddP p = Pts[PolyStart]; Pts.pop_back(); AddPoint(p); return; }
    ddShapePart s; s.K = ddShapePart::Poly; s.Closed = PolyClosed; s.Filled = PolyFilled; s.First = PolyStart; s.Count = n;
    for (int i = 0; i < n; i++) s.Box.Add(Pts[PolyStart + i]);
    Push(s);
  }
  /**
   * Adds a polygon / polyline from an array in one call (BeginPoly + PolyPush x n + EndPoly).
   */
  void AddPoly(const ddP *p, int n, bool closed, bool filled) {
    BeginPoly(closed, filled);
    for (int i = 0; i < n; i++) PolyPush(p[i]);
    EndPoly();
  }

 private:
  int PolyStart = 0; bool PolyClosed = false, PolyFilled = false;
  void Push(const ddShapePart &s) { Parts.push_back(s); Bounds.Add(s.Box); }
};

// ---------------------------------------------------------------- part helpers
/**
 * @return Number of edges of a part: 1 for a segment; n-1 for an open polyline; n for a closed polygon of 3 or more vertices; 0 otherwise.
 */
inline int ddPartEdges(const ddShapePart &p) {
  if (p.K == ddShapePart::Segment) return 1;
  if (p.K != ddShapePart::Poly || p.Count < 2) return 0;
  return (p.Closed && p.Count > 2) ? p.Count : p.Count - 1;
}
/**
 * Gets edge i of a part (0 <= i < ddPartEdges).
 * @param a,b  Outputs: the edge's end points.
 */
inline void ddPartEdge(const ddShapeSet &s, const ddShapePart &p, int i, ddP &a, ddP &b) {
  if (p.K == ddShapePart::Segment) { a = p.A; b = p.B; return; }
  a = s.Pts[p.First + i];
  b = s.Pts[p.First + (i + 1) % p.Count];
}
/**
 * A point that lies on the part's outline (a circle's rightmost point, a polygon's first vertex ...). Used for containment tests.
 */
inline ddP ddPartRep(const ddShapeSet &s, const ddShapePart &p) {
  switch (p.K) {
    case ddShapePart::Circle: return ddP(p.A[0] + p.R, p.A[1]);
    case ddShapePart::Poly:   return s.Pts[p.First];
    default:                  return p.A;
  }
}
/**
 * Is a point inside the SOLID region of a part (a disc, or a filled closed polygon)?
 * @param solid  Treat unfilled closed parts as solid too.
 * @param tol    Absolute tolerance (circles only).
 * @return false for points, segments, open polylines and unfilled parts unless solid is true.
 */
inline bool ddPartSolidContains(const ddShapeSet &s, const ddShapePart &p, const ddP &pt, bool solid, float tol) {
  if (p.K == ddShapePart::Circle) return (p.Filled || solid) && pt.DistanceTo(p.A) <= p.R + tol;
  if (p.K == ddShapePart::Poly)   return p.Closed && p.Count >= 3 && (p.Filled || solid) && ddPointInPolygon(pt, &s.Pts[p.First], p.Count);
  return false;
}

/**
 * Collector for contact points: stores up to `max` points in a caller array and merges points closer than `tol`
 * (a polygon corner is hit by two edges but reported once). Internal helper of the contact tests.
 */
struct ddEmit {
  ddP *out; int max; int n = 0; float tol;
  /**
   * @param o    Output array.
   * @param m    Capacity of o.
   * @param t    Merge tolerance (points closer than this count as one).
   */
  ddEmit(ddP *o, int m, float t) : out(o), max(m), tol(t) {}
  /**
   * @return true when the capacity is reached (callers stop searching).
   */
  bool Full() const { return n >= max; }
  /**
   * Adds a point unless it is a near-duplicate or the collector is full.
   */
  void Add(const ddP &p) {
    if (n >= max) return;
    for (int i = 0; i < n; i++) if (out[i].DistanceTo(p) <= tol) return;
    out[n++] = p;
  }
};

// ---------------------------------------------------------------- outline-vs-outline contacts
/**
 * Boundary contact points between two parts (outline against outline), pushed into an ddEmit.
 * Handles every pair of {point, circle rim, segment/polyline/polygon edges}. Parts are tested as OUTLINES only; for containment use ddShapeSetsOverlap.
 */
inline void ddPartContacts(const ddShapeSet &sa, const ddShapePart &a, const ddShapeSet &sb, const ddShapePart &b, ddEmit &em) {
  auto rank = [](const ddShapePart &p) { return p.K == ddShapePart::Point ? 0 : (p.K == ddShapePart::Circle ? 1 : 2); };
  if (rank(a) > rank(b)) { ddPartContacts(sb, b, sa, a, em); return; }
  ddP h1, h2, e0, e1, f0, f1;
  switch (rank(a) * 3 + rank(b)) {
    case 0: if (a.A.DistanceTo(b.A) <= em.tol) em.Add(a.A); break;                                       // point / point
    case 1: if (ddCollidePointCircle(a.A, b.A, b.R, true, em.tol)) em.Add(a.A); break;                    // point / circle rim
    case 2:                                                                                              // point / edges
      for (int i = 0, n = ddPartEdges(b); i < n && !em.Full(); i++) {
        ddPartEdge(sb, b, i, e0, e1);
        if (ddCollidePointLine(a.A, e0, e1, em.tol)) { em.Add(a.A); break; }
      }
      break;
    case 4: {                                                                                            // circle / circle
      int k = ddCollideCircleCircle(a.A, a.R, b.A, b.R, &h1, &h2);
      if (k == DD_INFINITE) em.Add(ddP(a.A[0] + a.R, a.A[1]));
      else { if (k >= 1) em.Add(h1); if (k == 2) em.Add(h2); }
      break;
    }
    case 5:                                                                                              // circle / edges
      for (int i = 0, n = ddPartEdges(b); i < n && !em.Full(); i++) {
        ddPartEdge(sb, b, i, e0, e1);
        int k = ddCollideLineCircle(e0, e1, a.A, a.R, &h1, &h2);
        if (k >= 1) em.Add(h1);
        if (k == 2) em.Add(h2);
      }
      break;
    case 8:                                                                                              // edges / edges
      for (int i = 0, na = ddPartEdges(a); i < na && !em.Full(); i++) {
        ddPartEdge(sa, a, i, e0, e1);
        ddBox eb = ddBox::FromCorners(e0, e1);
        for (int j = 0, nb = ddPartEdges(b); j < nb && !em.Full(); j++) {
          ddPartEdge(sb, b, j, f0, f1);
          if (!ddBoxesTouch(eb, ddBox::FromCorners(f0, f1), em.tol)) continue;
          int k = ddCollideLineLine(e0, e1, f0, f1, &h1, &h2);
          if (k >= 1) em.Add(h1);
          if (k == 2) em.Add(h2);
        }
      }
      break;
  }
}

/**
 * All boundary contact points between two shapes.
 * @param sa,sb   The shapes (world space).
 * @param out     Array receiving the points.
 * @param maxOut  Capacity of out (the search stops when it is full).
 * @param tol     Merge / touch tolerance (default DD_SHAPE_TOL).
 * @return Number of points stored, 0..maxOut.
 * @warning Pure containment has no boundary contact and returns 0; use ddShapeSetsOverlap for "do they overlap".
 */
inline int ddShapeSetsContacts(const ddShapeSet &sa, const ddShapeSet &sb, ddP *out, int maxOut, float tol = DD_SHAPE_TOL) {
  ddEmit em(out, maxOut, tol);
  if (!ddBoxesTouch(sa.Bounds, sb.Bounds, tol)) return 0;
  for (const ddShapePart &a : sa.Parts)
    for (const ddShapePart &b : sb.Parts) {
      if (em.Full()) return em.n;
      if (ddBoxesTouch(a.Box, b.Box, tol)) ddPartContacts(sa, a, sb, b, em);
    }
  return em.n;
}

/**
 * Do two shapes touch or overlap? True on any boundary contact, or when one lies wholly inside the other's solid region.
 * @param solid  false (default): honour each part's Filled flag. true: treat every closed shape as filled.
 * @param tol    Touch tolerance (default DD_SHAPE_TOL).
 * @note Early-outs on the first hit, so it is cheaper than counting contacts.
 */
inline bool ddShapeSetsOverlap(const ddShapeSet &sa, const ddShapeSet &sb, bool solid = false, float tol = DD_SHAPE_TOL) {
  if (!ddBoxesTouch(sa.Bounds, sb.Bounds, tol)) return false;
  for (const ddShapePart &a : sa.Parts)
    for (const ddShapePart &b : sb.Parts) {
      if (!ddBoxesTouch(a.Box, b.Box, tol)) continue;
      ddP tmp; ddEmit em(&tmp, 1, tol);
      ddPartContacts(sa, a, sb, b, em);
      if (em.n) return true;
      if (ddPartSolidContains(sa, a, ddPartRep(sb, b), solid, tol)) return true;    // b lies wholly inside a
      if (ddPartSolidContains(sb, b, ddPartRep(sa, a), solid, tol)) return true;    // a lies wholly inside b
    }
  return false;
}
