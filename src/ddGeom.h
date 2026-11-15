#pragma once
/**
 * @file ddGeom.h
 * Collision and measurement maths on points, lines, circles and axis-aligned boxes. Graphics-free: usable on its own.
 *
 * @par Conventions (all functions follow them)
 *   - Inputs are ddP values and floats. Results that are POINTS come back through optional pointers: pass nullptr to ignore.
 *   - Collision functions return the NUMBER of contact points (0, 1 or 2) and write them to p1 then p2. With 0 contacts
 *     NOTHING is modified; with 1 contact only p1 is written. This lets you pre-fill the outputs with a sentinel.
 *   - DD_INFINITE (-1) means "infinitely many contacts" (coincident shapes, or an unbounded overlap).
 *   - A "line" is two points A, B plus an ddExtent: the finite Segment A-B (default), the Ray from A through B, or the
 *     infinite Line through both. Contacts along a line are returned in order of travel from A toward B.
 *   - A circle is its OUTLINE for line/circle and circle/circle contacts: a chord lying wholly inside a circle touches
 *     nothing. Use ddCollidePointCircle (or ddCollideCircleBox) for the filled disc.
 *   - A box is SOLID for point/box, box/box and circle/box overlap tests, and its OUTLINE for ddCollideLineBox.
 *   - Tolerances are relative (DD_GEOM_TOL) to the LOCAL size of the shapes (differences between their points), not to
 *     their distance from the origin, so a small circle at x = 500 is judged as precisely as one at the origin.
 *
 * @warning Float precision is ~7 significant digits. Shapes that only just touch can report either 0 or 1 contact; do not
 *          build logic that depends on exact tangency. Treat a result near a boundary as "either".
 * @note Degenerate input is handled rather than rejected: a zero-length segment behaves as a point, a zero-radius circle
 *       as a point, a negative radius never collides.
 */
#include <cmath>
#include "ddPoint.h"

/**
 * How far a "line" runs between its two defining points A and B.
 *   - Segment: only the part from A to B (the default everywhere).
 *   - Ray: from A, through B, onwards to infinity (nothing behind A).
 *   - Line: infinite in both directions.
 */
enum struct ddExtent : uint8_t { Segment, Ray, Line };
/**
 * Returned by collision functions when there are infinitely many contact points (coincident circles, collinear infinite
 * lines, or rays overlapping without bound). It is negative, so `if (n > 0)` means "finite contacts found" and
 * `if (n != 0)` means "touching in some way".
 */
constexpr int   DD_INFINITE = -1;
/**
 * Relative tolerance (1e-5) used to decide tangency, endpoint touching and parallelism. It is scaled by the local size of
 * the shapes being tested.
 */
constexpr float DD_GEOM_TOL = 1e-5f;

// ---------------------------------------------------------------- vector basics
/**
 * Dot product of two vectors (treating the points as 2D vectors).
 * @return a.x*b.x + a.y*b.y. Positive when they point the same way, 0 when perpendicular.
 */
inline float ddDot(const ddP &a, const ddP &b)   { return a[0]*b[0] + a[1]*b[1]; }
/**
 * 2D cross product (the z component of the 3D cross product).
 * @return a.x*b.y - a.y*b.x. Positive when b is counter-clockwise from a. Its magnitude is the parallelogram area.
 */
inline float ddCross(const ddP &a, const ddP &b) { return a[0]*b[1] - a[1]*b[0]; }
/**
 * Largest absolute coordinate (the max-norm) of a point. A cheap size estimate used for tolerances, not a length.
 */
inline float ddMag(const ddP &p) { return fmaxf(fabsf(p[0]), fabsf(p[1])); }
/**
 * The point a + d*t: start at `a`, move along direction `d` by parameter `t`.
 * @param a  Start point.
 * @param d  Direction vector (e.g. b - a). Not normalised.
 * @param t  Parameter: 0 gives a, 1 gives a + d.
 */
inline ddP   ddAlong(const ddP &a, const ddP &d, float t) { return ddP(a[0] + d[0]*t, a[1] + d[1]*t); }   // a + d*t
/**
 * Midpoint of two points.
 */
inline ddP   ddMidpoint(const ddP &a, const ddP &b) { return ddP((a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f); }

/**
 * Which side of the DIRECTED line A->B a point is on.
 * @param p  The point.
 * @param a,b  The line (direction a to b).
 * @return +1 = left of the direction of travel (counter-clockwise, y up), -1 = right, 0 = on the line within tolerance.
 * @note Useful for orientation tests, convex-polygon containment and winding.
 */
inline int ddSide(const ddP &p, const ddP &a, const ddP &b) {
  ddP d = b - a;
  float len = hypotf(d[0], d[1]);
  ddP ap = p - a;
  float c = ddCross(d, ap);
  float tol = DD_GEOM_TOL * fmaxf(len, hypotf(ap[0], ap[1])) * fmaxf(len, 1e-12f);   // local scale, not distance from the origin
  return c > tol ? 1 : (c < -tol ? -1 : 0);
}

/**
 * Signed angle from direction A->B to direction C->D.
 * @return Radians in (-PI, PI]; counter-clockwise (y up) is positive.
 * @warning Returns 0 if either direction has zero length (atan2f(0, 0)). Check for degenerate input if that matters.
 */
inline float ddSignedAngle(const ddP &a, const ddP &b, const ddP &c, const ddP &d) {
  ddP u = b - a, v = d - c;
  return atan2f(ddCross(u, v), ddDot(u, v));
}
/**
 * Unsigned angle between directions A->B and C->D, in radians, 0..PI. Direction matters: reversing one line gives PI minus the angle.
 */
inline float ddAngle(const ddP &a, const ddP &b, const ddP &c, const ddP &d) { return fabsf(ddSignedAngle(a, b, c, d)); }

// ---------------------------------------------------------------- distances
/**
 * Distance from a point to a line (segment, ray or infinite line).
 * @param p        The point.
 * @param a,b      The line.
 * @param ext      Segment (default): the nearest point is clamped to the end points. Ray: clamped at a only. Line: unclamped.
 * @param closest  Optional output: the nearest point on the line.
 * @return         Distance >= 0.
 * @note This is the building block for point picking and hit tests.
 */
inline float ddDistPointLine(const ddP &p, const ddP &a, const ddP &b, ddExtent ext = ddExtent::Segment, ddP *closest = nullptr) {
  ddP d = b - a;
  float l2 = ddDot(d, d), t = 0.0f;
  if (l2 > 1e-24f) {
    t = ddDot(p - a, d) / l2;
    if (ext == ddExtent::Segment) { if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f; }
    else if (ext == ddExtent::Ray && t < 0.0f) t = 0.0f;
  }
  ddP q = ddAlong(a, d, t);
  if (closest) *closest = q;
  return p.DistanceTo(q);
}

/**
 * Signed distance from a point to a circle's outline.
 * @return Negative inside, 0 on the rim, positive outside. The magnitude is how far the point is from the rim.
 */
inline float ddSignedDistPointCircle(const ddP &p, const ddP &c, float r) { return p.DistanceTo(c) - r; }

/**
 * Gap between two circles' outlines.
 * @return Positive = they are apart by that much; 0 = touching; negative = overlapping by that depth.
 */
inline float ddGapCircleCircle(const ddP &c1, float r1, const ddP &c2, float r2) { return c1.DistanceTo(c2) - r1 - r2; }

/**
 * Gap between a line (segment / ray / line) and a circle's DISC.
 * @return Distance from the line to the disc. Negative means the line passes that deep into the disc (measured from the rim
 *         to the line's nearest point to the centre).
 */
inline float ddGapLineCircle(const ddP &a, const ddP &b, const ddP &c, float r, ddExtent ext = ddExtent::Segment) {
  return ddDistPointLine(c, a, b, ext) - r;
}

// ---------------------------------------------------------------- point collisions
/**
 * Is a point on a line, within a tolerance?
 * @param tol      Allowed distance in the same units as the points (default 1e-4). Absolute, not relative.
 * @param ext      Segment (default), Ray or Line.
 * @param closest  Optional output: the nearest point on the line.
 * @return 1 if the point is within tol of the line, else 0.
 * @note For mouse/touch picking pass a tolerance of a few pixels converted to world units.
 */
inline int ddCollidePointLine(const ddP &p, const ddP &a, const ddP &b, float tol = 1e-4f,
                              ddExtent ext = ddExtent::Segment, ddP *closest = nullptr) {
  return ddDistPointLine(p, a, b, ext, closest) <= tol ? 1 : 0;
}

/**
 * Is a point inside a circle?
 * @param outlineOnly  false (default): inside or on the disc. true: only on the rim (within tol).
 * @param tol          Absolute tolerance (default 1e-4).
 * @return 1 or 0.
 */
inline int ddCollidePointCircle(const ddP &p, const ddP &c, float r, bool outlineOnly = false, float tol = 1e-4f) {
  float d = p.DistanceTo(c);
  return outlineOnly ? (fabsf(d - r) <= tol ? 1 : 0) : (d <= r + tol ? 1 : 0);
}

/**
 * Even-odd point-in-polygon test (works for concave polygons).
 * @param p  The point.
 * @param v  Vertices in order (clockwise or counter-clockwise); the polygon is closed automatically.
 * @param n  Vertex count (3 or more for a meaningful result).
 * @warning Points exactly on an edge may come out either way. For an on-the-boundary test use ddCollidePointLine against the edges.
 * @warning Self-intersecting outlines leave "holes" where the winding is even.
 */
inline bool ddPointInPolygon(const ddP &p, const ddP *v, int n) {
  bool in = false;
  for (int i = 0, j = n - 1; i < n; j = i++)
    if (((v[i][1] > p[1]) != (v[j][1] > p[1])) &&
        (p[0] < (v[j][0]-v[i][0]) * (p[1]-v[i][1]) / (v[j][1]-v[i][1]) + v[i][0]))
      in = !in;
  return in;
}

// ---------------------------------------------------------------- line / circle
/**
 * Contacts between a line and a circle OUTLINE.
 * @param a,b     The line.
 * @param c,r     Circle centre and radius.
 * @param p1,p2   Optional outputs, ordered along a to b.
 * @param ext     Segment (default), Ray or Line.
 * @return        0 (miss), 1 (tangent, or only one crossing lies on the segment/ray), or 2.
 * @warning A segment that lies entirely INSIDE the circle touches nothing (returns 0). To detect "overlaps the disc" use
 *          ddGapLineCircle(...) < 0 or ddCollidePointCircle on its end points.
 * @note Segment end points exactly on the rim count as contacts.
 */
inline int ddCollideLineCircle(const ddP &a, const ddP &b, const ddP &c, float r,
                               ddP *p1 = nullptr, ddP *p2 = nullptr, ddExtent ext = ddExtent::Segment) {
  if (r < 0.0f) return 0;
  ddP d = b - a;
  float A = ddDot(d, d);
  float farthest = fmaxf(a.DistanceTo(c), b.DistanceTo(c));
  float tol = DD_GEOM_TOL * r + 1e-6f * farthest;          // geometry term + float noise of the computation
  if (A <= 1e-24f) {                                       // zero-length "segment": a single point
    if (fabsf(a.DistanceTo(c) - r) <= tol) { if (p1) *p1 = a; return 1; }
    return 0;
  }
  float len = sqrtf(A);
  float t0 = ddDot(c - a, d) / A;                          // parameter of the point closest to the centre
  ddP m = ddAlong(a, d, t0);
  float dist = m.DistanceTo(c);
  if (dist > r + tol) return 0;                            // misses
  float ts[2]; int n;
  if (dist >= r - tol) { ts[0] = t0; n = 1; }              // tangent
  else {
    float dt = sqrtf((r - dist) * (r + dist)) / len;       // half chord as a parameter step
    ts[0] = t0 - dt; ts[1] = t0 + dt; n = 2;
  }
  float tolT = tol / len;
  int found = 0;
  for (int i = 0; i < n; i++) {
    float t = ts[i];
    if (ext != ddExtent::Line) {
      if (t < -tolT) continue;
      if (ext == ddExtent::Segment && t > 1.0f + tolT) continue;
      if (t < 0.0f) t = 0.0f;
      if (ext == ddExtent::Segment && t > 1.0f) t = 1.0f;
    }
    ddP q = ddAlong(a, d, t);
    if (found == 0) { if (p1) *p1 = q; }
    else            { if (p2) *p2 = q; }
    found++;
  }
  return found;
}

// ---------------------------------------------------------------- line / line
/**
 * Contacts between two lines (each a segment, ray or infinite line).
 * @param a,b       First line.   @param c,d  Second line.
 * @param p1,p2     Optional outputs.
 * @param extAB,extCD  Extents of the first and second line (default Segment).
 * @return  0 = no contact (parallel and apart, or the crossing lies outside an extent);
 *          1 = one crossing or touching point -> p1;
 *          2 = collinear overlap of finite length -> p1, p2 are the ends of the shared stretch;
 *          DD_INFINITE = collinear with an unbounded overlap (infinite lines, or rays pointing the same way).
 * @warning Almost-parallel lines (sine of the angle under about 1e-5) are treated as parallel.
 * @note Zero-length segments act as points.
 */
inline int ddCollideLineLine(const ddP &a, const ddP &b, const ddP &c, const ddP &d,
                             ddP *p1 = nullptr, ddP *p2 = nullptr,
                             ddExtent extAB = ddExtent::Segment, ddExtent extCD = ddExtent::Segment) {
  ddP r = b - a, s = d - c;
  float scale = fmaxf(fmaxf(hypotf(r[0], r[1]), hypotf(s[0], s[1])), fmaxf(a.DistanceTo(c), a.DistanceTo(d)));
  float tol = DD_GEOM_TOL * scale;
  float rl = hypotf(r[0], r[1]), sl = hypotf(s[0], s[1]);
  const float tiny = 1e-12f;

  if (rl <= tiny && sl <= tiny) { if (a.DistanceTo(c) <= tol) { if (p1) *p1 = a; return 1; } return 0; }
  if (rl <= tiny) { if (ddDistPointLine(a, c, d, extCD) <= tol) { if (p1) *p1 = a; return 1; } return 0; }
  if (sl <= tiny) { if (ddDistPointLine(c, a, b, extAB) <= tol) { if (p1) *p1 = c; return 1; } return 0; }

  float rxs = ddCross(r, s);
  ddP ca = c - a;
  if (fabsf(rxs) <= 1e-5f * rl * sl) {                      // parallel
    if (fabsf(ddCross(ca, r)) > tol * rl) return 0;        // parallel but apart
    // collinear: intersect the parameter ranges along A-B
    float r2 = rl * rl;
    float tc = ddDot(ca, r) / r2, td = ddDot(d - a, r) / r2;
    const float inf = INFINITY;
    float loA = (extAB == ddExtent::Line) ? -inf : 0.0f, hiA = (extAB == ddExtent::Segment) ? 1.0f : inf;
    float loC, hiC;
    if (extCD == ddExtent::Line) { loC = -inf; hiC = inf; }
    else if (extCD == ddExtent::Segment) { loC = fminf(tc, td); hiC = fmaxf(tc, td); }
    else if (td >= tc) { loC = tc; hiC = inf; }
    else { loC = -inf; hiC = tc; }
    float lo = fmaxf(loA, loC), hi = fminf(hiA, hiC);
    float tolT = tol / rl;
    if (lo > hi + tolT) return 0;
    if (std::isinf(lo) || std::isinf(hi)) return DD_INFINITE;
    if (hi - lo <= tolT) { if (p1) *p1 = ddAlong(a, r, (lo + hi) * 0.5f); return 1; }
    if (p1) *p1 = ddAlong(a, r, lo);
    if (p2) *p2 = ddAlong(a, r, hi);
    return 2;
  }

  float t = ddCross(ca, s) / rxs;                           // a + t*r == c + u*s
  float u = ddCross(ca, r) / rxs;
  float tolT = tol / rl, tolU = tol / sl;
  if (extAB != ddExtent::Line) { if (t < -tolT) return 0; if (extAB == ddExtent::Segment && t > 1.0f + tolT) return 0; }
  if (extCD != ddExtent::Line) { if (u < -tolU) return 0; if (extCD == ddExtent::Segment && u > 1.0f + tolU) return 0; }
  if (extAB != ddExtent::Line) { if (t < 0.0f) t = 0.0f; if (extAB == ddExtent::Segment && t > 1.0f) t = 1.0f; }
  if (p1) *p1 = ddAlong(a, r, t);
  return 1;
}

/**
 * Shortest distance between two SEGMENTS.
 * @param p1,p2  Optional outputs: the closest point on each segment (equal when they cross).
 * @return 0 if they touch or cross, otherwise the gap.
 */
inline float ddDistSegSeg(const ddP &a, const ddP &b, const ddP &c, const ddP &d, ddP *p1 = nullptr, ddP *p2 = nullptr) {
  ddP hit;
  if (ddCollideLineLine(a, b, c, d, &hit) > 0) { if (p1) *p1 = hit; if (p2) *p2 = hit; return 0.0f; }
  ddP q, best1, best2; float bd, dd;
  bd = ddDistPointLine(a, c, d, ddExtent::Segment, &q); best1 = a; best2 = q;
  dd = ddDistPointLine(b, c, d, ddExtent::Segment, &q); if (dd < bd) { bd = dd; best1 = b; best2 = q; }
  dd = ddDistPointLine(c, a, b, ddExtent::Segment, &q); if (dd < bd) { bd = dd; best1 = q; best2 = c; }
  dd = ddDistPointLine(d, a, b, ddExtent::Segment, &q); if (dd < bd) { bd = dd; best1 = q; best2 = d; }
  if (p1) *p1 = best1;
  if (p2) *p2 = best2;
  return bd;
}

// ---------------------------------------------------------------- circle / circle
/**
 * Contacts between two circle OUTLINES.
 * @param c1,r1,c2,r2  The circles.
 * @param p1,p2        Optional outputs. With two contacts, p1 is on the LEFT of the direction c1->c2 and p2 on the right.
 * @return 0 (apart, or one inside the other), 1 (tangent), 2, or DD_INFINITE (the same circle).
 * @warning Nested circles (one wholly inside the other without touching) return 0 contacts even though they overlap as
 *          discs. Compare ddGapCircleCircle(...) < 0 for disc overlap.
 */
inline int ddCollideCircleCircle(const ddP &c1, float r1, const ddP &c2, float r2, ddP *p1 = nullptr, ddP *p2 = nullptr) {
  if (r1 < 0.0f || r2 < 0.0f) return 0;
  ddP dv = c2 - c1;
  float d = hypotf(dv[0], dv[1]);
  float tol = DD_GEOM_TOL * fmaxf(fmaxf(r1, r2), d);
  if (d <= tol) return (fabsf(r1 - r2) <= tol) ? DD_INFINITE : 0;   // same centre: same circle or nested
  if (d > r1 + r2 + tol) return 0;                                    // apart
  if (d < fabsf(r1 - r2) - tol) return 0;                             // one inside the other
  float a = (r1 * r1 - r2 * r2 + d * d) / (2.0f * d);                 // distance from c1 to the chord
  float h2 = r1 * r1 - a * a;
  ddP m = ddAlong(c1, dv, a / d);
  if (h2 <= tol * tol || fabsf(d - (r1 + r2)) <= tol || fabsf(d - fabsf(r1 - r2)) <= tol) { if (p1) *p1 = m; return 1; }
  float h = sqrtf(h2) / d;
  if (p1) *p1 = ddP(m[0] - dv[1] * h, m[1] + dv[0] * h);
  if (p2) *p2 = ddP(m[0] + dv[1] * h, m[1] - dv[0] * h);
  return 2;
}

// ================================================================ axis-aligned box
/**
 * Axis-aligned box: min/max corners with X0 <= X1 and Y0 <= Y1.
 *
 * A default-constructed box is EMPTY (inverted infinities) so that Add() can grow it from nothing. It is the standard
 * broad-phase shape: cheap to build, cheap to test. Used internally for culling and as the first rejection test in
 * shape-to-shape collision.
 *
 * @code
 * ddBox b;                         // empty
 * b.Add(ddP(1, 2)); b.Add(ddP(5, -1));
 * float w = b.Width();             // 4
 * @endcode
 *
 * @warning An empty box has Width()/Height() 0 but is NOT a zero-size box: every collision test with it returns "no".
 * @warning The (x0,y0,x1,y1) constructor sorts its arguments, but the public fields are NOT re-sorted if you assign to
 *          them directly.
 */
struct ddBox {
  float X0 = INFINITY, Y0 = INFINITY, X1 = -INFINITY, Y1 = -INFINITY;
  /**
   * Creates an EMPTY box.
   */
  ddBox() = default;
  /**
   * Creates a box from two opposite corners given in any order (they are sorted).
   */
  ddBox(float x0, float y0, float x1, float y1) : X0(fminf(x0, x1)), Y0(fminf(y0, y1)), X1(fmaxf(x0, x1)), Y1(fmaxf(y0, y1)) {}
  /**
   * Box spanned by two corner points (any order).
   */
  static ddBox FromCorners(const ddP &a, const ddP &b) { return ddBox(a[0], a[1], b[0], b[1]); }
  /**
   * Box from a centre and half extents. @param halfW,halfH Half the width and height.
   */
  static ddBox FromCenter(const ddP &c, float halfW, float halfH) { return ddBox(c[0] - halfW, c[1] - halfH, c[0] + halfW, c[1] + halfH); }

  /**
   * @return true if the box contains nothing (never had a point added).
   */
  bool  Empty() const { return X0 > X1 || Y0 > Y1; }
  /**
   * @return X1 - X0, or 0 for an empty box.
   */
  float Width() const { return Empty() ? 0.0f : X1 - X0; }
  /**
   * @return Y1 - Y0, or 0 for an empty box.
   */
  float Height() const { return Empty() ? 0.0f : Y1 - Y0; }
  /**
   * @return The centre point. Meaningless for an empty box.
   */
  ddP   Center() const { return ddP((X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f); }
  /**
   * Corner i, counter-clockwise from (X0, Y0): 0 = bottom-left, 1 = bottom-right, 2 = top-right, 3 = top-left (y up).
   * @warning The index is not range-checked beyond the 0..3 pattern; use 0..3.
   */
  ddP   Corner(int i) const { return ddP((i == 1 || i == 2) ? X1 : X0, (i >= 2) ? Y1 : Y0); }   // counter-clockwise from (X0,Y0)
  /**
   * Grows the box to include a point.
   */
  void  Add(const ddP &p) { if (p[0] < X0) X0 = p[0]; if (p[0] > X1) X1 = p[0]; if (p[1] < Y0) Y0 = p[1]; if (p[1] > Y1) Y1 = p[1]; }
  /**
   * Grows the box to include another box (an empty box adds nothing).
   */
  void  Add(const ddBox &b) { if (b.Empty()) return; if (b.X0 < X0) X0 = b.X0; if (b.X1 > X1) X1 = b.X1; if (b.Y0 < Y0) Y0 = b.Y0; if (b.Y1 > Y1) Y1 = b.Y1; }
  /**
   * @return A copy expanded by m on every side (shrunk if m is negative). An empty box stays empty.
   */
  ddBox Grown(float m) const { return Empty() ? *this : ddBox(X0 - m, Y0 - m, X1 + m, Y1 + m); }
  /**
   * Is the point inside or on the box?
   * @param tol  Extra margin on every side (default 0).
   */
  bool  Contains(const ddP &p, float tol = 0.0f) const { return p[0] >= X0 - tol && p[0] <= X1 + tol && p[1] >= Y0 - tol && p[1] <= Y1 + tol; }
};

/**
 * Broad-phase test: do two boxes overlap or touch?
 * @param tol  Extra margin (default 0). Pass a small positive value so that shapes that merely touch are not rejected.
 * @return false if either box is empty.
 */
inline bool ddBoxesTouch(const ddBox &a, const ddBox &b, float tol = 0.0f) {
  return !(a.Empty() || b.Empty() || a.X1 < b.X0 - tol || a.X0 > b.X1 + tol || a.Y1 < b.Y0 - tol || a.Y0 > b.Y1 + tol);
}

/**
 * Nearest point of the SOLID box to p. Returns p itself when it is inside.
 */
inline ddP ddClosestPointOnBox(const ddP &p, const ddBox &b) {
  return ddP(fminf(fmaxf(p[0], b.X0), b.X1), fminf(fmaxf(p[1], b.Y0), b.Y1));
}
/**
 * Distance from a point to the solid box (0 inside or on it).
 */
inline float ddDistPointBox(const ddP &p, const ddBox &b) { return p.DistanceTo(ddClosestPointOnBox(p, b)); }

/**
 * Point inside or on the solid box (within tol). @return 1 or 0.
 */
inline int ddCollidePointBox(const ddP &p, const ddBox &b, float tol = 1e-4f) { return b.Contains(p, tol) ? 1 : 0; }

/**
 * Do two solid boxes overlap or touch?
 * @param overlap  Optional output: the shared region.
 * @param tol      Extra margin (default 0).
 * @return 1 or 0. (Box overlap is a region, not points, so the count is only ever 0 or 1.)
 */
inline int ddCollideBoxBox(const ddBox &a, const ddBox &b, ddBox *overlap = nullptr, float tol = 0.0f) {
  if (!ddBoxesTouch(a, b, tol)) return 0;
  if (overlap) *overlap = ddBox(fmaxf(a.X0, b.X0), fmaxf(a.Y0, b.Y0), fminf(a.X1, b.X1), fminf(a.Y1, b.Y1));
  return 1;
}

/**
 * Contacts between a line and a box OUTLINE: where it enters and leaves.
 * @param a,b     The line.   @param box The box.
 * @param p1,p2   Optional outputs, ordered along a to b.
 * @param ext     Segment (default), Ray or Line.
 * @return 0, 1 or 2. A line running ALONG an edge returns 2 (the ends of the shared stretch).
 * @warning A segment wholly inside the box touches no outline (0). To clip a segment to the solid box, test its end points
 *          with ddCollidePointBox as well.
 */
inline int ddCollideLineBox(const ddP &a, const ddP &b, const ddBox &box, ddP *p1 = nullptr, ddP *p2 = nullptr,
                            ddExtent ext = ddExtent::Segment) {
  if (box.Empty()) return 0;
  ddP d = b - a;
  float len = hypotf(d[0], d[1]);
  float tol = DD_GEOM_TOL * fmaxf(fmaxf(box.Width(), box.Height()), len) + 1e-6f * fmaxf(ddMag(a - box.Center()), ddMag(b - box.Center()));
  if (len <= 1e-12f) {                                              // zero-length: a point on the outline?
    bool in = box.Contains(a, tol);
    bool inner = a[0] > box.X0 + tol && a[0] < box.X1 - tol && a[1] > box.Y0 + tol && a[1] < box.Y1 - tol;
    if (in && !inner) { if (p1) *p1 = a; return 1; }
    return 0;
  }
  float tmin = -INFINITY, tmax = INFINITY;
  for (int ax = 0; ax < 2; ax++) {                                 // slab method
    float lo = ax ? box.Y0 : box.X0, hi = ax ? box.Y1 : box.X1;
    if (fabsf(d[ax]) <= 1e-12f * len) {
      if (a[ax] < lo - tol || a[ax] > hi + tol) return 0;          // parallel to the slab and outside it
    } else {
      float t1 = (lo - a[ax]) / d[ax], t2 = (hi - a[ax]) / d[ax];
      if (t1 > t2) { float s = t1; t1 = t2; t2 = s; }
      if (t1 > tmin) tmin = t1;
      if (t2 < tmax) tmax = t2;
    }
  }
  float tolT = tol / len;
  if (tmin > tmax + tolT) return 0;
  float ts[2]; int n;
  if (tmax - tmin <= tolT) { ts[0] = (tmin + tmax) * 0.5f; n = 1; } else { ts[0] = tmin; ts[1] = tmax; n = 2; }
  int found = 0;
  for (int i = 0; i < n; i++) {
    float t = ts[i];
    if (std::isinf(t)) continue;
    if (ext != ddExtent::Line) {
      if (t < -tolT) continue;
      if (ext == ddExtent::Segment && t > 1.0f + tolT) continue;
      if (t < 0.0f) t = 0.0f;
      if (ext == ddExtent::Segment && t > 1.0f) t = 1.0f;
    }
    ddP q = ddAlong(a, d, t);
    if (found == 0) { if (p1) *p1 = q; } else { if (p2) *p2 = q; }
    found++;
  }
  return found;
}

/**
 * Does the DISC overlap the SOLID box? (The classic circle-vs-rectangle test.)
 * @param closest  Optional output: the box point nearest the circle centre.
 * @return 1 if they overlap or touch, else 0.
 */
inline int ddCollideCircleBox(const ddP &c, float r, const ddBox &box, ddP *closest = nullptr) {
  if (box.Empty() || r < 0.0f) return 0;
  ddP q = ddClosestPointOnBox(c, box);
  if (closest) *closest = q;
  return c.DistanceTo(q) <= r ? 1 : 0;
}

/**
 * Contact points between a circle OUTLINE and a box OUTLINE (up to 8).
 * @param out     Optional array receiving the points.
 * @param maxOut  Capacity of out. Pass 0 (with out = nullptr) to just count.
 * @return Number of distinct contact points found (corner hits are merged). May exceed maxOut only when you ask to count.
 */
inline int ddCollideCircleBoxOutline(const ddP &c, float r, const ddBox &box, ddP *out = nullptr, int maxOut = 0) {
  if (box.Empty() || r < 0.0f) return 0;
  int n = 0;
  float tol = 1e-4f * fmaxf(1.0f, r);
  for (int e = 0; e < 4; e++) {
    ddP h[2];
    int k = ddCollideLineCircle(box.Corner(e), box.Corner((e + 1) & 3), c, r, &h[0], &h[1]);
    for (int i = 0; i < k; i++) {
      bool dup = false;                                            // a corner hit is reported by both of its edges
      for (int j = 0; j < n && j < maxOut; j++) if (out[j].DistanceTo(h[i]) <= tol) { dup = true; break; }
      if (dup) continue;
      if (out && n < maxOut) out[n] = h[i];
      n++;
    }
  }
  return n;
}

/**
 * The smallest axis-aligned box containing a circle.
 */
inline ddBox ddBoxOfCircle(const ddP &c, float r) { return ddBox(c[0] - r, c[1] - r, c[0] + r, c[1] + r); }
