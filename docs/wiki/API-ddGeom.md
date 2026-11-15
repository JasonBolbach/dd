# ddGeom.h

*Collision and measurement maths.*  Source: [`src/ddGeom.h`](https://github.com/JasonBolbach/dd/blob/main/src/ddGeom.h)

**Conventions (all functions follow them)**

- Inputs are [ddP](API-ddPoint#struct-ddp) values and floats. Results that are POINTS come back through optional pointers: pass nullptr to ignore.
- Collision functions return the NUMBER of contact points (0, 1 or 2) and write them to p1 then p2. With 0 contacts NOTHING is modified; with 1 contact only p1 is written. This lets you pre-fill the outputs with a sentinel.
- [DD_INFINITE](API-ddGeom#dd_infinite) (-1) means "infinitely many contacts" (coincident shapes, or an unbounded overlap).
- A "line" is two points A, B plus an [ddExtent](API-ddGeom#enum-ddextent): the finite Segment A-B (default), the Ray from A through B, or the infinite Line through both. Contacts along a line are returned in order of travel from A toward B.
- A circle is its OUTLINE for line/circle and circle/circle contacts: a chord lying wholly inside a circle touches nothing. Use [ddCollidePointCircle](API-ddGeom#ddcollidepointcircle) (or [ddCollideCircleBox](API-ddGeom#ddcollidecirclebox)) for the filled disc.
- A box is SOLID for point/box, box/box and circle/box overlap tests, and its OUTLINE for [ddCollideLineBox](API-ddGeom#ddcollidelinebox).
- Tolerances are relative ([DD_GEOM_TOL](API-ddGeom#dd_geom_tol)) to the LOCAL size of the shapes (differences between their points), not to their distance from the origin, so a small circle at x = 500 is judged as precisely as one at the origin.

**Pitfalls**

- Float precision is ~7 significant digits. Shapes that only just touch can report either 0 or 1 contact; do not build logic that depends on exact tangency. Treat a result near a boundary as "either".

**Tips and notes**

- Degenerate input is handled rather than rejected: a zero-length segment behaves as a point, a zero-radius circle as a point, a negative radius never collides.

## Contents

- [enum ddExtent](#enum-ddextent)
- [DD_INFINITE](#dd_infinite)
- [DD_GEOM_TOL](#dd_geom_tol)
- [ddDot()](#dddot)
- [ddCross()](#ddcross)
- [ddMag()](#ddmag)
- [ddAlong()](#ddalong)
- [ddMidpoint()](#ddmidpoint)
- [ddSide()](#ddside)
- [ddSignedAngle()](#ddsignedangle)
- [ddAngle()](#ddangle)
- [ddDistPointLine()](#dddistpointline)
- [ddSignedDistPointCircle()](#ddsigneddistpointcircle)
- [ddGapCircleCircle()](#ddgapcirclecircle)
- [ddGapLineCircle()](#ddgaplinecircle)
- [ddCollidePointLine()](#ddcollidepointline)
- [ddCollidePointCircle()](#ddcollidepointcircle)
- [ddPointInPolygon()](#ddpointinpolygon)
- [ddCollideLineCircle()](#ddcollidelinecircle)
- [ddCollideLineLine()](#ddcollidelineline)
- [ddDistSegSeg()](#dddistsegseg)
- [ddCollideCircleCircle()](#ddcollidecirclecircle)
- [struct ddBox](#struct-ddbox)
- [ddBoxesTouch()](#ddboxestouch)
- [ddClosestPointOnBox()](#ddclosestpointonbox)
- [ddDistPointBox()](#dddistpointbox)
- [ddCollidePointBox()](#ddcollidepointbox)
- [ddCollideBoxBox()](#ddcollideboxbox)
- [ddCollideLineBox()](#ddcollidelinebox)
- [ddCollideCircleBox()](#ddcollidecirclebox)
- [ddCollideCircleBoxOutline()](#ddcollidecircleboxoutline)
- [ddBoxOfCircle()](#ddboxofcircle)

## enum ddExtent

```cpp
enum struct ddExtent : uint8_t
```

How far a "line" runs between its two defining points A and B.

- Segment: only the part from A to B (the default everywhere).
- Ray: from A, through B, onwards to infinity (nothing behind A).
- Line: infinite in both directions.

---
## DD_INFINITE

```cpp
constexpr int DD_INFINITE = -1
```

Returned by collision functions when there are infinitely many contact points (coincident circles, collinear infinite
lines, or rays overlapping without bound). It is negative, so `if (n > 0)` means "finite contacts found" and
`if (n != 0)` means "touching in some way".

---
## DD_GEOM_TOL

```cpp
constexpr float DD_GEOM_TOL = 1e-5f
```

Relative tolerance (1e-5) used to decide tangency, endpoint touching and parallelism. It is scaled by the local size of
the shapes being tested.

---
## ddDot()

```cpp
inline float ddDot(const ddP &a, const ddP &b)
```

Dot product of two vectors (treating the points as 2D vectors).

**Returns:** a.x*b.x + a.y*b.y. Positive when they point the same way, 0 when perpendicular.

---
## ddCross()

```cpp
inline float ddCross(const ddP &a, const ddP &b)
```

2D cross product (the z component of the 3D cross product).

**Returns:** a.x*b.y - a.y*b.x. Positive when b is counter-clockwise from a. Its magnitude is the parallelogram area.

---
## ddMag()

```cpp
inline float ddMag(const ddP &p)
```

Largest absolute coordinate (the max-norm) of a point. A cheap size estimate used for tolerances, not a length.

---
## ddAlong()

```cpp
inline ddP ddAlong(const ddP &a, const ddP &d, float t)
```

The point a + d*t: start at `a`, move along direction `d` by parameter `t`.

| Parameter | Description |
|---|---|
| `a` | Start point. |
| `d` | Direction vector (e.g. b - a). Not normalised. |
| `t` | Parameter: 0 gives a, 1 gives a + d. |

---
## ddMidpoint()

```cpp
inline ddP ddMidpoint(const ddP &a, const ddP &b)
```

Midpoint of two points.

---
## ddSide()

```cpp
inline int ddSide(const ddP &p, const ddP &a, const ddP &b)
```

Which side of the DIRECTED line A->B a point is on.

| Parameter | Description |
|---|---|
| `p` | The point. |
| `a,b` | The line (direction a to b). |

**Returns:** +1 = left of the direction of travel (counter-clockwise, y up), -1 = right, 0 = on the line within tolerance.

**Tips and notes**

- Useful for orientation tests, convex-polygon containment and winding.

---
## ddSignedAngle()

```cpp
inline float ddSignedAngle(const ddP &a, const ddP &b, const ddP &c, const ddP &d)
```

Signed angle from direction A->B to direction C->D.

**Returns:** Radians in (-PI, PI]; counter-clockwise (y up) is positive.

**Pitfalls**

- Returns 0 if either direction has zero length (atan2f(0, 0)). Check for degenerate input if that matters.

---
## ddAngle()

```cpp
inline float ddAngle(const ddP &a, const ddP &b, const ddP &c, const ddP &d)
```

Unsigned angle between directions A->B and C->D, in radians, 0..PI. Direction matters: reversing one line gives PI minus the angle.

---
## ddDistPointLine()

```cpp
inline float ddDistPointLine(const ddP &p, const ddP &a, const ddP &b, ddExtent ext = ddExtent::Segment, ddP *closest = nullptr)
```

Distance from a point to a line (segment, ray or infinite line).

| Parameter | Description |
|---|---|
| `p` | The point. |
| `a,b` | The line. |
| `ext` | Segment (default): the nearest point is clamped to the end points. Ray: clamped at a only. Line: unclamped. |
| `closest` | Optional output: the nearest point on the line. |

**Returns:** Distance >= 0.

**Tips and notes**

- This is the building block for point picking and hit tests.

---
## ddSignedDistPointCircle()

```cpp
inline float ddSignedDistPointCircle(const ddP &p, const ddP &c, float r)
```

Signed distance from a point to a circle's outline.

**Returns:** Negative inside, 0 on the rim, positive outside. The magnitude is how far the point is from the rim.

---
## ddGapCircleCircle()

```cpp
inline float ddGapCircleCircle(const ddP &c1, float r1, const ddP &c2, float r2)
```

Gap between two circles' outlines.

**Returns:** Positive = they are apart by that much; 0 = touching; negative = overlapping by that depth.

---
## ddGapLineCircle()

```cpp
inline float ddGapLineCircle(const ddP &a, const ddP &b, const ddP &c, float r, ddExtent ext = ddExtent::Segment)
```

Gap between a line (segment / ray / line) and a circle's DISC.

**Returns:** Distance from the line to the disc. Negative means the line passes that deep into the disc (measured from the rim         to the line's nearest point to the centre).

---
## ddCollidePointLine()

```cpp
inline int ddCollidePointLine(const ddP &p, const ddP &a, const ddP &b, float tol = 1e-4f, ddExtent ext = ddExtent::Segment, ddP *closest = nullptr)
```

Is a point on a line, within a tolerance?

| Parameter | Description |
|---|---|
| `tol` | Allowed distance in the same units as the points (default 1e-4). Absolute, not relative. |
| `ext` | Segment (default), Ray or Line. |
| `closest` | Optional output: the nearest point on the line. |

**Returns:** 1 if the point is within tol of the line, else 0.

**Tips and notes**

- For mouse/touch picking pass a tolerance of a few pixels converted to world units.

---
## ddCollidePointCircle()

```cpp
inline int ddCollidePointCircle(const ddP &p, const ddP &c, float r, bool outlineOnly = false, float tol = 1e-4f)
```

Is a point inside a circle?

| Parameter | Description |
|---|---|
| `outlineOnly` | false (default): inside or on the disc. true: only on the rim (within tol). |
| `tol` | Absolute tolerance (default 1e-4). |

**Returns:** 1 or 0.

---
## ddPointInPolygon()

```cpp
inline bool ddPointInPolygon(const ddP &p, const ddP *v, int n)
```

Even-odd point-in-polygon test (works for concave polygons).

| Parameter | Description |
|---|---|
| `p` | The point. |
| `v` | Vertices in order (clockwise or counter-clockwise); the polygon is closed automatically. |
| `n` | Vertex count (3 or more for a meaningful result). |

**Pitfalls**

- Points exactly on an edge may come out either way. For an on-the-boundary test use [ddCollidePointLine](API-ddGeom#ddcollidepointline) against the edges.
- Self-intersecting outlines leave "holes" where the winding is even.

---
## ddCollideLineCircle()

```cpp
inline int ddCollideLineCircle(const ddP &a, const ddP &b, const ddP &c, float r, ddP *p1 = nullptr, ddP *p2 = nullptr, ddExtent ext = ddExtent::Segment)
```

Contacts between a line and a circle OUTLINE.

| Parameter | Description |
|---|---|
| `a,b` | The line. |
| `c,r` | Circle centre and radius. |
| `p1,p2` | Optional outputs, ordered along a to b. |
| `ext` | Segment (default), Ray or Line. |

**Returns:** 0 (miss), 1 (tangent, or only one crossing lies on the segment/ray), or 2.

**Pitfalls**

- A segment that lies entirely INSIDE the circle touches nothing (returns 0). To detect "overlaps the disc" use [ddGapLineCircle](API-ddGeom#ddgaplinecircle)(...) < 0 or [ddCollidePointCircle](API-ddGeom#ddcollidepointcircle) on its end points.

**Tips and notes**

- Segment end points exactly on the rim count as contacts.

---
## ddCollideLineLine()

```cpp
inline int ddCollideLineLine(const ddP &a, const ddP &b, const ddP &c, const ddP &d, ddP *p1 = nullptr, ddP *p2 = nullptr, ddExtent extAB = ddExtent::Segment, ddExtent extCD = ddExtent::Segment)
```

Contacts between two lines (each a segment, ray or infinite line).

| Parameter | Description |
|---|---|
| `a,b` | First line. |
| `c,d` | Second line. |
| `p1,p2` | Optional outputs. |
| `extAB,extCD` | Extents of the first and second line (default Segment). |

**Returns:** 0 = no contact (parallel and apart, or the crossing lies outside an extent);          1 = one crossing or touching point -> p1;          2 = collinear overlap of finite length -> p1, p2 are the ends of the shared stretch;          [DD_INFINITE](API-ddGeom#dd_infinite) = collinear with an unbounded overlap (infinite lines, or rays pointing the same way).

**Pitfalls**

- Almost-parallel lines (sine of the angle under about 1e-5) are treated as parallel.

**Tips and notes**

- Zero-length segments act as points.

---
## ddDistSegSeg()

```cpp
inline float ddDistSegSeg(const ddP &a, const ddP &b, const ddP &c, const ddP &d, ddP *p1 = nullptr, ddP *p2 = nullptr)
```

Shortest distance between two SEGMENTS.

| Parameter | Description |
|---|---|
| `p1,p2` | Optional outputs: the closest point on each segment (equal when they cross). |

**Returns:** 0 if they touch or cross, otherwise the gap.

---
## ddCollideCircleCircle()

```cpp
inline int ddCollideCircleCircle(const ddP &c1, float r1, const ddP &c2, float r2, ddP *p1 = nullptr, ddP *p2 = nullptr)
```

Contacts between two circle OUTLINES.

| Parameter | Description |
|---|---|
| `c1,r1,c2,r2` | The circles. |
| `p1,p2` | Optional outputs. With two contacts, p1 is on the LEFT of the direction c1->c2 and p2 on the right. |

**Returns:** 0 (apart, or one inside the other), 1 (tangent), 2, or [DD_INFINITE](API-ddGeom#dd_infinite) (the same circle).

**Pitfalls**

- Nested circles (one wholly inside the other without touching) return 0 contacts even though they overlap as discs. Compare [ddGapCircleCircle](API-ddGeom#ddgapcirclecircle)(...) < 0 for disc overlap.

---
## struct ddBox

```cpp
struct ddBox
```

Axis-aligned box: min/max corners with X0 <= X1 and Y0 <= Y1.

A default-constructed box is EMPTY (inverted infinities) so that Add() can grow it from nothing. It is the standard
broad-phase shape: cheap to build, cheap to test. Used internally for culling and as the first rejection test in
shape-to-shape collision.

```cpp
ddBox b;                         // empty
b.Add(ddP(1, 2)); b.Add(ddP(5, -1));
float w = b.Width();             // 4
```

**Pitfalls**

- An empty box has Width()/Height() 0 but is NOT a zero-size box: every collision test with it returns "no".
- The (x0,y0,x1,y1) constructor sorts its arguments, but the public fields are NOT re-sorted if you assign to them directly.

---
### ddBox::ddBox()

```cpp
ddBox() = default
```

Creates an EMPTY box.


### ddBox::ddBox() (overload 2)

```cpp
ddBox(float x0, float y0, float x1, float y1)
```

Creates a box from two opposite corners given in any order (they are sorted).


### ddBox::FromCorners()

```cpp
static ddBox FromCorners(const ddP &a, const ddP &b)
```

Box spanned by two corner points (any order).


### ddBox::FromCenter()

```cpp
static ddBox FromCenter(const ddP &c, float halfW, float halfH)
```

Box from a centre and half extents.

| Parameter | Description |
|---|---|
| `halfW,halfH` | Half the width and height. |


### ddBox::Empty()

```cpp
bool Empty() const
```

**Returns:** true if the box contains nothing (never had a point added).


### ddBox::Width()

```cpp
float Width() const
```

**Returns:** X1 - X0, or 0 for an empty box.


### ddBox::Height()

```cpp
float Height() const
```

**Returns:** Y1 - Y0, or 0 for an empty box.


### ddBox::Center()

```cpp
ddP Center() const
```

**Returns:** The centre point. Meaningless for an empty box.


### ddBox::Corner()

```cpp
ddP Corner(int i) const
```

Corner i, counter-clockwise from (X0, Y0): 0 = bottom-left, 1 = bottom-right, 2 = top-right, 3 = top-left (y up).

**Pitfalls**

- The index is not range-checked beyond the 0..3 pattern; use 0..3.


### ddBox::Add()

```cpp
void Add(const ddP &p)
```

Grows the box to include a point.


### ddBox::Add() (overload 2)

```cpp
void Add(const ddBox &b)
```

Grows the box to include another box (an empty box adds nothing).


### ddBox::Grown()

```cpp
ddBox Grown(float m) const
```

**Returns:** A copy expanded by m on every side (shrunk if m is negative). An empty box stays empty.


### ddBox::Contains()

```cpp
bool Contains(const ddP &p, float tol = 0.0f) const
```

Is the point inside or on the box?

| Parameter | Description |
|---|---|
| `tol` | Extra margin on every side (default 0). |


## ddBoxesTouch()

```cpp
inline bool ddBoxesTouch(const ddBox &a, const ddBox &b, float tol = 0.0f)
```

Broad-phase test: do two boxes overlap or touch?

| Parameter | Description |
|---|---|
| `tol` | Extra margin (default 0). Pass a small positive value so that shapes that merely touch are not rejected. |

**Returns:** false if either box is empty.

---
## ddClosestPointOnBox()

```cpp
inline ddP ddClosestPointOnBox(const ddP &p, const ddBox &b)
```

Nearest point of the SOLID box to p. Returns p itself when it is inside.

---
## ddDistPointBox()

```cpp
inline float ddDistPointBox(const ddP &p, const ddBox &b)
```

Distance from a point to the solid box (0 inside or on it).

---
## ddCollidePointBox()

```cpp
inline int ddCollidePointBox(const ddP &p, const ddBox &b, float tol = 1e-4f)
```

Point inside or on the solid box (within tol). @return 1 or 0.

---
## ddCollideBoxBox()

```cpp
inline int ddCollideBoxBox(const ddBox &a, const ddBox &b, ddBox *overlap = nullptr, float tol = 0.0f)
```

Do two solid boxes overlap or touch?

| Parameter | Description |
|---|---|
| `overlap` | Optional output: the shared region. |
| `tol` | Extra margin (default 0). |

**Returns:** 1 or 0. (Box overlap is a region, not points, so the count is only ever 0 or 1.)

---
## ddCollideLineBox()

```cpp
inline int ddCollideLineBox(const ddP &a, const ddP &b, const ddBox &box, ddP *p1 = nullptr, ddP *p2 = nullptr, ddExtent ext = ddExtent::Segment)
```

Contacts between a line and a box OUTLINE: where it enters and leaves.

| Parameter | Description |
|---|---|
| `a,b` | The line. |
| `box` | The box. |
| `p1,p2` | Optional outputs, ordered along a to b. |
| `ext` | Segment (default), Ray or Line. |

**Returns:** 0, 1 or 2. A line running ALONG an edge returns 2 (the ends of the shared stretch).

**Pitfalls**

- A segment wholly inside the box touches no outline (0). To clip a segment to the solid box, test its end points with [ddCollidePointBox](API-ddGeom#ddcollidepointbox) as well.

---
## ddCollideCircleBox()

```cpp
inline int ddCollideCircleBox(const ddP &c, float r, const ddBox &box, ddP *closest = nullptr)
```

Does the DISC overlap the SOLID box? (The classic circle-vs-rectangle test.)

| Parameter | Description |
|---|---|
| `closest` | Optional output: the box point nearest the circle centre. |

**Returns:** 1 if they overlap or touch, else 0.

---
## ddCollideCircleBoxOutline()

```cpp
inline int ddCollideCircleBoxOutline(const ddP &c, float r, const ddBox &box, ddP *out = nullptr, int maxOut = 0)
```

Contact points between a circle OUTLINE and a box OUTLINE (up to 8).

| Parameter | Description |
|---|---|
| `out` | Optional array receiving the points. |
| `maxOut` | Capacity of out. Pass 0 (with out = nullptr) to just count. |

**Returns:** Number of distinct contact points found (corner hits are merged). May exceed maxOut only when you ask to count.

---
## ddBoxOfCircle()

```cpp
inline ddBox ddBoxOfCircle(const ddP &c, float r)
```

The smallest axis-aligned box containing a circle.

---
