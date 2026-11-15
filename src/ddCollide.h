#pragma once
// Shape-to-shape collision for scene primitives (ddVector.h), built on ddGeom.h / ddShapeSet.h.
//
//   bool ddOverlap(a, b)                        do they touch or overlap?
//   int  ddCollide(a, b, &p1, &p2)              number of boundary contact points; first two returned
//   int  ddContacts(a, b, out, maxOut)          the same, all of them into an array
//   int  ddFindOverlaps(scene, p, out, maxOut)  every scene primitive that overlaps p
//
// Primitives are tested where they are drawn: transform and group nesting are applied. Both
// primitives are taken to be top-level (parent = world) unless you pass their parent matrices.
// Hidden (Visible = false) primitives have no collision geometry.
//
// An unfilled shape is only its outline (a small unfilled circle inside a big one does not touch it);
// a filled shape is solid. Pass solid = true to treat every closed shape as filled.
// Contacts are boundary points only; wholly-contained shapes have none, so use ddOverlap for those.
// Circles under a non-uniform scale / skew, arcs and Beziers are tested as polylines.
// Not thread-safe: the scratch buffers are shared.
#include "ddVector.h"

inline ddShapeSet &ddScratchShapes(int i) { static ddShapeSet s[2]; return s[i]; }

inline bool ddOverlap(ddVectorPrimitive *a, ddVectorPrimitive *b, bool solid = false,
                      const Matrix3x3 &parentA = Matrix3x3(), const Matrix3x3 &parentB = Matrix3x3()) {
  if (!a || !b) return false;
  ddShapeSet &sa = ddScratchShapes(0), &sb = ddScratchShapes(1);
  sa.Clear(); sb.Clear();
  a->CollectShapes(parentA, sa);
  b->CollectShapes(parentB, sb);
  return ddShapeSetsOverlap(sa, sb, solid);
}

inline int ddContacts(ddVectorPrimitive *a, ddVectorPrimitive *b, ddP *out, int maxOut, bool solid = false,
                      const Matrix3x3 &parentA = Matrix3x3(), const Matrix3x3 &parentB = Matrix3x3()) {
  (void)solid;                                             // contacts are boundary points either way
  if (!a || !b || !out || maxOut <= 0) return 0;
  ddShapeSet &sa = ddScratchShapes(0), &sb = ddScratchShapes(1);
  sa.Clear(); sb.Clear();
  a->CollectShapes(parentA, sa);
  b->CollectShapes(parentB, sb);
  return ddShapeSetsContacts(sa, sb, out, maxOut);
}

// Boundary contact count, capped at 64. The first two points go to p1 / p2 (either may be nullptr).
inline int ddCollide(ddVectorPrimitive *a, ddVectorPrimitive *b, ddP *p1 = nullptr, ddP *p2 = nullptr, bool solid = false,
                     const Matrix3x3 &parentA = Matrix3x3(), const Matrix3x3 &parentB = Matrix3x3()) {
  ddP pts[64];
  int n = ddContacts(a, b, pts, 64, solid, parentA, parentB);
  if (n >= 1 && p1) *p1 = pts[0];
  if (n >= 2 && p2) *p2 = pts[1];
  return n;
}

// Every visible primitive of `scene` (top level) that overlaps `p`, excluding p itself. Returns how many
// were stored (at most maxOut). Cheap bounding-box rejection first, so large scenes are fine.
inline int ddFindOverlaps(RenderBlock &scene, ddVectorPrimitive *p, ddVectorPrimitive **out, int maxOut,
                          bool solid = false, const Matrix3x3 &parent = Matrix3x3()) {
  if (!p || !out || maxOut <= 0) return 0;
  ddShapeSet &sp = ddScratchShapes(0), &sq = ddScratchShapes(1);
  sp.Clear();
  p->CollectShapes(parent, sp);
  if (sp.Empty()) return 0;
  ddBox pb = sp.Bounds.Grown(DD_SHAPE_TOL);
  int n = 0;
  for (ddVectorPrimitive *q = scene.First(); q && n < maxOut; q = scene.Next(q)) {
    if (q == p || !q->Visible) continue;
    float x0, y0, x1, y1;
    if (q->ParentBounds(x0, y0, x1, y1) && !ddBoxesTouch(pb, ddBox(x0, y0, x1, y1))) continue;    // cheap reject
    sq.Clear();
    q->CollectShapes(Matrix3x3(), sq);
    if (!sq.Empty() && ddShapeSetsOverlap(sp, sq, solid)) out[n++] = q;
  }
  return n;
}
