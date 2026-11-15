#pragma once
/**
 * @file ddCollide.h
 * Shape-to-shape collision between scene primitives (ddVector.h), built on ddGeom.h and ddShapeSet.h.
 *
 * @code
 * bool hit = ddOverlap(a, b);                        // do they touch or overlap?
 * ddP p1, p2;
 * int  n   = ddCollide(a, b, &p1, &p2);              // number of boundary contact points; the first two returned
 * ddP  pts[16];
 * int  m   = ddContacts(a, b, pts, 16);              // ...or all of them
 * ddVectorPrimitive *found[8];
 * int  k   = ddFindOverlaps(scene, a, found, 8);     // everything in the scene that a touches
 * @endcode
 *
 * Primitives are tested where they are DRAWN: their transform (and group nesting) is applied. Both primitives are taken to be top-level
 * (parent = world) unless you pass their parent matrices.
 *
 * @par Semantics
 *   - An unfilled shape is only its outline: a small unfilled circle inside a big one does not touch it. A filled shape is solid.
 *     Pass solid = true to treat every closed shape as filled.
 *   - Contacts are BOUNDARY points. A shape wholly inside another has none: use ddOverlap for that.
 *   - Hidden (Visible = false) primitives have no collision geometry. ddText and ddGrid never collide.
 *   - Circles under a non-uniform scale or skew, arcs and Beziers are tested as polylines (an approximation).
 *
 * @warning NOT thread-safe: the functions share two static scratch ShapeSets.
 * @warning The scratch sets are cleared and refilled on every call, so these functions are not re-entrant either (do not call them from inside a primitive's Shapes()).
 * @note For many objects against one, use ddFindOverlaps: it builds the probe's geometry once and rejects with bounding boxes first.
 */
#include "ddVector.h"

/**
 * The two shared scratch shape sets used by the functions below (index 0 and 1). Exposed for advanced use; do not hold references across calls.
 */
inline ddShapeSet &ddScratchShapes(int i) { static ddShapeSet s[2]; return s[i]; }

/**
 * Do two primitives touch or overlap?
 * @param a,b        The primitives.
 * @param solid      false (default): filled shapes are solid, unfilled ones are outlines. true: every closed shape counts as solid.
 * @param parentA,parentB  World-from-parent matrices (identity for top-level primitives).
 * @return true on any boundary contact or if one lies inside the other's solid region. False if either pointer is null or hidden.
 * @note The cheapest of the collision calls (stops at the first hit).
 */
inline bool ddOverlap(ddVectorPrimitive *a, ddVectorPrimitive *b, bool solid = false,
                      const Matrix3x3 &parentA = Matrix3x3(), const Matrix3x3 &parentB = Matrix3x3()) {
  if (!a || !b) return false;
  ddShapeSet &sa = ddScratchShapes(0), &sb = ddScratchShapes(1);
  sa.Clear(); sb.Clear();
  a->CollectShapes(parentA, sa);
  b->CollectShapes(parentB, sb);
  return ddShapeSetsOverlap(sa, sb, solid);
}

/**
 * Boundary contact points between two primitives.
 * @param out     Array receiving the points.
 * @param maxOut  Capacity of out.
 * @param solid   Accepted for symmetry with ddOverlap; contact points are boundary points either way.
 * @return Number of distinct points stored (0..maxOut).
 * @warning A shape wholly inside another gives 0 here although ddOverlap would say true.
 */
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

/**
 * Boundary contact count, with the first two points returned through pointers (the convention of ddGeom.h).
 * @param p1,p2  Optional outputs for the first two contacts. Untouched when there are fewer.
 * @return The number of contact points found, counted up to 64 (so a result above 2 means more contacts than the two returned).
 * @note Use ddContacts if you need all of the points.
 */
inline int ddCollide(ddVectorPrimitive *a, ddVectorPrimitive *b, ddP *p1 = nullptr, ddP *p2 = nullptr, bool solid = false,
                     const Matrix3x3 &parentA = Matrix3x3(), const Matrix3x3 &parentB = Matrix3x3()) {
  ddP pts[64];
  int n = ddContacts(a, b, pts, 64, solid, parentA, parentB);
  if (n >= 1 && p1) *p1 = pts[0];
  if (n >= 2 && p2) *p2 = pts[1];
  return n;
}

/**
 * Finds every visible top-level primitive of a scene that overlaps a probe primitive.
 * @param scene   The scene to search.
 * @param p       The probe (excluded from the results).
 * @param out     Array of pointers receiving the matches.
 * @param maxOut  Capacity of out (the search stops when it is full).
 * @param solid   As for ddOverlap.
 * @param parent  World-from-parent matrix of the probe (identity if it is top-level).
 * @return Number of matches stored.
 * @note Candidates are first rejected by bounding box (ParentBounds), so it scales well to large scenes. Primitives without bounds are always tested.
 */
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
