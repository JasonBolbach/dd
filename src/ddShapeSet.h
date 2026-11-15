#pragma once
// World-space collision geometry for shape-to-shape tests, and the pairwise tests on it.
// No graphics dependency. Primitives (ddVector.h) fill a ddShapeSet from their Shapes() method;
// ddCollide.h wraps it with the primitive-level API.
//
// A shape is a set of PARTS:
//   Point    a single point
//   Circle   centre + radius, Filled = a disc, otherwise just the rim
//   Segment  one line segment
//   Poly     vertices; Closed polygons are solid when Filled, open ones are polylines
//
// Semantics (the same as how the shapes are drawn):
//   - an unfilled shape is only its outline, so a small unfilled circle inside a big one does NOT touch it;
//   - a filled shape is solid, so the same circle inside a filled one DOES overlap it;
//   - pass solid = true to treat every closed shape as filled (physics-style "do these occupy common space").
//   - "Contacts" are boundary contact points (outline crossings / touches). Pure containment has no
//     boundary contact: use Overlap for that.
#include <vector>
#include "ddGeom.h"

constexpr float DD_SHAPE_TOL = 1e-4f;       // world units: touching within this counts as touching

struct ddShapePart {
  enum Kind : uint8_t { Point, Circle, Segment, Poly };
  Kind K = Point;
  bool Filled = false, Closed = false;
  ddP  A, B;                  // Point/Circle: A = centre; Segment: A -> B
  float R = 0.0f;             // Circle radius
  int  First = 0, Count = 0;  // Poly: vertex range in ddShapeSet::Pts
  ddBox Box;                  // bounds of this part
};

class ddShapeSet {
 public:
  std::vector<ddShapePart> Parts;
  std::vector<ddP> Pts;
  ddBox Bounds;               // bounds of everything

  void Clear() { Parts.clear(); Pts.clear(); Bounds = ddBox(); }
  bool Empty() const { return Parts.empty(); }

  void AddPoint(const ddP &p) {
    ddShapePart s; s.K = ddShapePart::Point; s.A = p; s.Box = ddBox(p[0], p[1], p[0], p[1]);
    Push(s);
  }
  void AddCircle(const ddP &c, float r, bool filled) {
    if (r < 0.0f) return;
    ddShapePart s; s.K = ddShapePart::Circle; s.A = c; s.R = r; s.Filled = filled; s.Box = ddBoxOfCircle(c, r);
    Push(s);
  }
  void AddSegment(const ddP &a, const ddP &b) {
    ddShapePart s; s.K = ddShapePart::Segment; s.A = a; s.B = b; s.Box = ddBox::FromCorners(a, b);
    Push(s);
  }
  // Polygons / polylines are built a vertex at a time so nothing needs a temporary array.
  void BeginPoly(bool closed, bool filled) {
    PolyStart = (int)Pts.size(); PolyClosed = closed; PolyFilled = filled;
  }
  void PolyPush(const ddP &p) { Pts.push_back(p); }
  void EndPoly() {
    int n = (int)Pts.size() - PolyStart;
    if (n <= 0) return;
    if (n == 1) { ddP p = Pts[PolyStart]; Pts.pop_back(); AddPoint(p); return; }
    ddShapePart s; s.K = ddShapePart::Poly; s.Closed = PolyClosed; s.Filled = PolyFilled; s.First = PolyStart; s.Count = n;
    for (int i = 0; i < n; i++) s.Box.Add(Pts[PolyStart + i]);
    Push(s);
  }
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
inline int ddPartEdges(const ddShapePart &p) {
  if (p.K == ddShapePart::Segment) return 1;
  if (p.K != ddShapePart::Poly || p.Count < 2) return 0;
  return (p.Closed && p.Count > 2) ? p.Count : p.Count - 1;
}
inline void ddPartEdge(const ddShapeSet &s, const ddShapePart &p, int i, ddP &a, ddP &b) {
  if (p.K == ddShapePart::Segment) { a = p.A; b = p.B; return; }
  a = s.Pts[p.First + i];
  b = s.Pts[p.First + (i + 1) % p.Count];
}
// A point that lies on the part's outline (used for containment tests).
inline ddP ddPartRep(const ddShapeSet &s, const ddShapePart &p) {
  switch (p.K) {
    case ddShapePart::Circle: return ddP(p.A[0] + p.R, p.A[1]);
    case ddShapePart::Poly:   return s.Pts[p.First];
    default:                  return p.A;
  }
}
// Is pt inside the SOLID region of the part (discs and filled closed polygons)?
inline bool ddPartSolidContains(const ddShapeSet &s, const ddShapePart &p, const ddP &pt, bool solid, float tol) {
  if (p.K == ddShapePart::Circle) return (p.Filled || solid) && pt.DistanceTo(p.A) <= p.R + tol;
  if (p.K == ddShapePart::Poly)   return p.Closed && p.Count >= 3 && (p.Filled || solid) && ddPointInPolygon(pt, &s.Pts[p.First], p.Count);
  return false;
}

// Collects distinct contact points up to `max` (nearby duplicates, e.g. a polygon corner hit by two edges, are merged).
struct ddEmit {
  ddP *out; int max; int n = 0; float tol;
  ddEmit(ddP *o, int m, float t) : out(o), max(m), tol(t) {}
  bool Full() const { return n >= max; }
  void Add(const ddP &p) {
    if (n >= max) return;
    for (int i = 0; i < n; i++) if (out[i].DistanceTo(p) <= tol) return;
    out[n++] = p;
  }
};

// ---------------------------------------------------------------- outline-vs-outline contacts
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

// All boundary contact points between two shapes (up to maxOut). Returns how many were stored.
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

// Do the two shapes touch or overlap (boundary contact, or one inside the other's solid region)?
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
