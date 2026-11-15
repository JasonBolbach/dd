#pragma once
// 2D collision + measurement primitives on points, lines and circles. No graphics dependency.
//
// Conventions
//  - Inputs are ddP values / floats. Outputs are optional pointers (pass nullptr to ignore).
//  - Collision functions return the NUMBER of contact points (0, 1, 2) and write them through the
//    pointers in order: first point -> p1, second -> p2. With 0 contacts nothing is modified;
//    with 1 contact only p1 is written.
//  - DD_INFINITE (-1) means "infinitely many" (coincident shapes / unbounded overlap).
//  - A "line" is described by two points A,B and an ddExtent: the finite Segment A-B (default),
//    the Ray from A through B, or the infinite Line through both.
//  - A circle is its OUTLINE for line/circle and circle/circle contacts (a chord that lies fully
//    inside the circle touches nothing). Use ddCollidePointCircle for the filled disc.
//  - Tolerances are relative (DD_GEOM_TOL) to the LOCAL size of the shapes (differences between
//    their points), not to how far they sit from the origin, so a small circle at x = 500 is judged
//    as precisely as one at the origin.
#include <cmath>
#include "ddPoint.h"

enum struct ddExtent : uint8_t { Segment, Ray, Line };
constexpr int   DD_INFINITE = -1;
constexpr float DD_GEOM_TOL = 1e-5f;

// ---------------------------------------------------------------- vector basics
inline float ddDot(const ddP &a, const ddP &b)   { return a[0]*b[0] + a[1]*b[1]; }
inline float ddCross(const ddP &a, const ddP &b) { return a[0]*b[1] - a[1]*b[0]; }
inline float ddMag(const ddP &p) { return fmaxf(fabsf(p[0]), fabsf(p[1])); }
inline ddP   ddAlong(const ddP &a, const ddP &d, float t) { return ddP(a[0] + d[0]*t, a[1] + d[1]*t); }   // a + d*t
inline ddP   ddMidpoint(const ddP &a, const ddP &b) { return ddP((a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f); }

// Which side of the directed line A->B the point is on: +1 left, -1 right, 0 on the line.
inline int ddSide(const ddP &p, const ddP &a, const ddP &b) {
  ddP d = b - a;
  float len = hypotf(d[0], d[1]);
  ddP ap = p - a;
  float c = ddCross(d, ap);
  float tol = DD_GEOM_TOL * fmaxf(len, hypotf(ap[0], ap[1])) * fmaxf(len, 1e-12f);   // local scale, not distance from the origin
  return c > tol ? 1 : (c < -tol ? -1 : 0);
}

// Angle between directions A->B and C->D, 0..PI radians; and the signed (counter-clockwise +) version.
inline float ddSignedAngle(const ddP &a, const ddP &b, const ddP &c, const ddP &d) {
  ddP u = b - a, v = d - c;
  return atan2f(ddCross(u, v), ddDot(u, v));
}
inline float ddAngle(const ddP &a, const ddP &b, const ddP &c, const ddP &d) { return fabsf(ddSignedAngle(a, b, c, d)); }

// ---------------------------------------------------------------- distances
// Distance from p to the line A-B (extent decides segment / ray / infinite). `closest` gets the nearest point.
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

// Signed distance from p to the circle outline: negative inside, positive outside, 0 on it.
inline float ddSignedDistPointCircle(const ddP &p, const ddP &c, float r) { return p.DistanceTo(c) - r; }

// Gap between two circles' outlines (negative = overlap depth, 0 = touching).
inline float ddGapCircleCircle(const ddP &c1, float r1, const ddP &c2, float r2) { return c1.DistanceTo(c2) - r1 - r2; }

// Gap between segment/ray/line A-B and the circle's disc (negative = the line passes through the disc).
inline float ddGapLineCircle(const ddP &a, const ddP &b, const ddP &c, float r, ddExtent ext = ddExtent::Segment) {
  return ddDistPointLine(c, a, b, ext) - r;
}

// ---------------------------------------------------------------- point collisions
// Point on the line A-B within `tol` (world units). Returns 1/0; `closest` as in ddDistPointLine.
inline int ddCollidePointLine(const ddP &p, const ddP &a, const ddP &b, float tol = 1e-4f,
                              ddExtent ext = ddExtent::Segment, ddP *closest = nullptr) {
  return ddDistPointLine(p, a, b, ext, closest) <= tol ? 1 : 0;
}

// Point against a circle. outlineOnly=false: inside or on the disc. outlineOnly=true: on the rim (within tol).
inline int ddCollidePointCircle(const ddP &p, const ddP &c, float r, bool outlineOnly = false, float tol = 1e-4f) {
  float d = p.DistanceTo(c);
  return outlineOnly ? (fabsf(d - r) <= tol ? 1 : 0) : (d <= r + tol ? 1 : 0);
}

// Even-odd point-in-polygon.
inline bool ddPointInPolygon(const ddP &p, const ddP *v, int n) {
  bool in = false;
  for (int i = 0, j = n - 1; i < n; j = i++)
    if (((v[i][1] > p[1]) != (v[j][1] > p[1])) &&
        (p[0] < (v[j][0]-v[i][0]) * (p[1]-v[i][1]) / (v[j][1]-v[i][1]) + v[i][0]))
      in = !in;
  return in;
}

// ---------------------------------------------------------------- line / circle
// Contacts between segment/ray/line A-B and a circle outline: 0, 1 (tangent or one crossing) or 2.
// Points come back ordered along A->B.
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
// Contacts between A-B and C-D (each segment, ray or infinite line). Returns:
//   0            no contact (parallel and apart, or the crossing lies outside an extent)
//   1            crossing / touching point -> p1
//   2            collinear overlap of finite length -> p1,p2 are the ends of the shared stretch
//   DD_INFINITE  collinear and the overlap is unbounded (infinite lines, or rays pointing the same way)
// Zero-length segments behave as points.
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

// Shortest distance between two SEGMENTS A-B and C-D. 0 if they touch/cross. p1 / p2 receive the
// closest point on each (equal when they cross).
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
// Contacts between two circle outlines: 0, 1 (tangent) or 2. With two, p1 is on the LEFT of the
// direction c1->c2 and p2 on the right. DD_INFINITE if they are the same circle.
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
// Min/max corners (X0<=X1, Y0<=Y1). A default box is EMPTY (inverted), so Add() can grow it from nothing.
struct ddBox {
  float X0 = INFINITY, Y0 = INFINITY, X1 = -INFINITY, Y1 = -INFINITY;
  ddBox() = default;
  ddBox(float x0, float y0, float x1, float y1) : X0(fminf(x0, x1)), Y0(fminf(y0, y1)), X1(fmaxf(x0, x1)), Y1(fmaxf(y0, y1)) {}
  static ddBox FromCorners(const ddP &a, const ddP &b) { return ddBox(a[0], a[1], b[0], b[1]); }
  static ddBox FromCenter(const ddP &c, float halfW, float halfH) { return ddBox(c[0] - halfW, c[1] - halfH, c[0] + halfW, c[1] + halfH); }

  bool  Empty() const { return X0 > X1 || Y0 > Y1; }
  float Width() const { return Empty() ? 0.0f : X1 - X0; }
  float Height() const { return Empty() ? 0.0f : Y1 - Y0; }
  ddP   Center() const { return ddP((X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f); }
  ddP   Corner(int i) const { return ddP((i == 1 || i == 2) ? X1 : X0, (i >= 2) ? Y1 : Y0); }   // counter-clockwise from (X0,Y0)
  void  Add(const ddP &p) { if (p[0] < X0) X0 = p[0]; if (p[0] > X1) X1 = p[0]; if (p[1] < Y0) Y0 = p[1]; if (p[1] > Y1) Y1 = p[1]; }
  void  Add(const ddBox &b) { if (b.Empty()) return; if (b.X0 < X0) X0 = b.X0; if (b.X1 > X1) X1 = b.X1; if (b.Y0 < Y0) Y0 = b.Y0; if (b.Y1 > Y1) Y1 = b.Y1; }
  ddBox Grown(float m) const { return Empty() ? *this : ddBox(X0 - m, Y0 - m, X1 + m, Y1 + m); }
  bool  Contains(const ddP &p, float tol = 0.0f) const { return p[0] >= X0 - tol && p[0] <= X1 + tol && p[1] >= Y0 - tol && p[1] <= Y1 + tol; }
};

// Box rejection test: do the boxes overlap or touch (within tol)?
inline bool ddBoxesTouch(const ddBox &a, const ddBox &b, float tol = 0.0f) {
  return !(a.Empty() || b.Empty() || a.X1 < b.X0 - tol || a.X0 > b.X1 + tol || a.Y1 < b.Y0 - tol || a.Y0 > b.Y1 + tol);
}

// Nearest point of the SOLID box to p (p itself when inside).
inline ddP ddClosestPointOnBox(const ddP &p, const ddBox &b) {
  return ddP(fminf(fmaxf(p[0], b.X0), b.X1), fminf(fmaxf(p[1], b.Y0), b.Y1));
}
// Distance from p to the solid box (0 inside / on it).
inline float ddDistPointBox(const ddP &p, const ddBox &b) { return p.DistanceTo(ddClosestPointOnBox(p, b)); }

// Point inside or on the solid box (within tol). Returns 1/0.
inline int ddCollidePointBox(const ddP &p, const ddBox &b, float tol = 1e-4f) { return b.Contains(p, tol) ? 1 : 0; }

// Box vs box (solid). Returns 1 if they overlap or touch, 0 otherwise; `overlap` gets the shared region.
inline int ddCollideBoxBox(const ddBox &a, const ddBox &b, ddBox *overlap = nullptr, float tol = 0.0f) {
  if (!ddBoxesTouch(a, b, tol)) return 0;
  if (overlap) *overlap = ddBox(fmaxf(a.X0, b.X0), fmaxf(a.Y0, b.Y0), fminf(a.X1, b.X1), fminf(a.Y1, b.Y1));
  return 1;
}

// Segment/ray/line A-B against the box OUTLINE: 0, 1 or 2 contact points ordered along A->B
// (the points where it enters / leaves the box). A line running along an edge reports the two ends
// of the shared stretch. Same conventions as ddCollideLineCircle.
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

// Does the SOLID box overlap the DISC (centre c, radius r)? Returns 1/0; `closest` gets the box point nearest the centre.
inline int ddCollideCircleBox(const ddP &c, float r, const ddBox &box, ddP *closest = nullptr) {
  if (box.Empty() || r < 0.0f) return 0;
  ddP q = ddClosestPointOnBox(c, box);
  if (closest) *closest = q;
  return c.DistanceTo(q) <= r ? 1 : 0;
}

// Contact points between a circle OUTLINE and the box OUTLINE (up to 8). Fills out[0..maxOut) and
// returns the number found (may exceed maxOut only if you pass maxOut = 0 to just count).
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

// Box that just contains a circle.
inline ddBox ddBoxOfCircle(const ddP &c, float r) { return ddBox(c[0] - r, c[1] - r, c[0] + r, c[1] + r); }
