# ddShapeSet.h

*Collision geometry for shapes.*  Source: [`src/ddShapeSet.h`](https://github.com/JasonBolbach/dd/blob/main/src/ddShapeSet.h)

Primitives (ddVector.h) fill a [ddShapeSet](API-ddShapeSet#class-ddshapeset) from their Shapes() method; [ddCollide](API-ddCollide#ddcollide).h wraps it with the primitive-level API.
You only need this header directly to build custom collision queries or to test geometry you describe yourself.

**A shape is a set of PARTS**

- Point    a single point
- Circle   centre + radius; Filled = a disc, otherwise just the rim
- Segment  one line segment
- Poly     vertices; Closed polygons are solid when Filled, open ones are polylines

**Semantics (the same as how the shapes are drawn)**

- an unfilled shape is only its OUTLINE, so a small unfilled circle inside a big one does NOT touch it;
- a filled shape is SOLID, so the same circle inside a filled one DOES overlap it;
- pass solid = true to treat every closed shape as filled (physics-style "do these occupy common space");
- "contacts" are boundary contact points (outline crossings or touches). Pure containment has no boundary contact: use Overlap for that.

**Pitfalls**

- All tests are O(parts_a x parts_b) with a bounding-box rejection per pair. Shapes with thousands of edges are slow to compare; simplify them or test against their bounds first.

**Tips and notes**

- Everything is in WORLD space. The primitives apply their transform when they build the set.

## Contents

- [DD_SHAPE_TOL](#dd_shape_tol)
- [struct ddShapePart](#struct-ddshapepart)
- [enum Kind](#enum-kind)
- [class ddShapeSet](#class-ddshapeset)
- [ddPartEdges()](#ddpartedges)
- [ddPartEdge()](#ddpartedge)
- [ddPartRep()](#ddpartrep)
- [ddPartSolidContains()](#ddpartsolidcontains)
- [struct ddEmit](#struct-ddemit)
- [ddPartContacts()](#ddpartcontacts)
- [ddShapeSetsContacts()](#ddshapesetscontacts)
- [ddShapeSetsOverlap()](#ddshapesetsoverlap)

## DD_SHAPE_TOL

```cpp
constexpr float DD_SHAPE_TOL = 1e-4f
```

World-unit tolerance (1e-4): shapes touching within this distance count as touching, and contact points closer than this are merged.

---
## struct ddShapePart

```cpp
struct ddShapePart
```

One piece of a shape: a point, circle, segment or polygon/polyline.

**Fields**

- `K` kind; `Filled` solid (circles, closed polys); `Closed` (polys); `A`,`B` centre / end points; `R` circle radius; `First`,`Count` vertex range of a Poly in the set's `Pts`; `Box` bounds.

**Tips and notes**

- Built through [ddShapeSet](API-ddShapeSet#class-ddshapeset)::Add*; you normally only read them.

---
## enum Kind

```cpp
enum Kind : uint8_t
```

Kinds of part: a single Point, a Circle (rim or disc), a line Segment, or a Poly (polygon / polyline).

---
## class ddShapeSet

```cpp
class ddShapeSet
```

A list of shape parts with their combined bounds: the world-space collision geometry of one primitive (or any shape you assemble).

```cpp
ddShapeSet s;
s.AddCircle(ddP(0, 0), 5.0f, true);                     // a disc
s.AddSegment(ddP(-10, 0), ddP(10, 0));                  // plus a segment
bool hit = ddShapeSetsOverlap(s, other);
```

**Pitfalls**

- Uses std::vector (heap). The first fill allocates; keep sets around rather than creating them per call on a microcontroller.

**Tips and notes**

- Reuse one set and call Clear() between uses: the internal vectors keep their capacity, so there is no allocation after warm-up.

---
### Parts

```cpp
std::vector<ddShapePart> Parts
```

The parts, in the order added.


### Pts

```cpp
std::vector<ddP> Pts
```

Vertex storage shared by all Poly parts (each Poly refers to a range).


### Bounds

```cpp
ddBox Bounds
```

Bounding box of every part (empty for an empty set). Used as the first, cheapest rejection test.


### ddShapeSet::Clear()

```cpp
void Clear()
```

Removes all parts (capacity is kept).


### ddShapeSet::Empty()

```cpp
bool Empty() const
```

**Returns:** true if there are no parts.


### ddShapeSet::AddPoint()

```cpp
void AddPoint(const ddP &p)
```

Adds a point part.


### ddShapeSet::AddCircle()

```cpp
void AddCircle(const ddP &c, float r, bool filled)
```

Adds a circle.

| Parameter | Description |
|---|---|
| `c` | Centre. |
| `r` | Radius (a negative radius adds nothing). |
| `filled` | true = disc, false = rim only. |


### ddShapeSet::AddSegment()

```cpp
void AddSegment(const ddP &a, const ddP &b)
```

Adds a line segment from a to b.


### ddShapeSet::BeginPoly()

```cpp
void BeginPoly(bool closed, bool filled)
```

Starts a polygon / polyline. Follow with PolyPush() for each vertex and EndPoly().

| Parameter | Description |
|---|---|
| `closed` | true = polygon, false = open polyline. |
| `filled` | true = solid (only meaningful when closed). |

**Pitfalls**

- Do not interleave other Add* calls between BeginPoly and EndPoly.


### ddShapeSet::PolyPush()

```cpp
void PolyPush(const ddP &p)
```

Adds a vertex to the polygon started by BeginPoly().


### ddShapeSet::EndPoly()

```cpp
void EndPoly()
```

Finishes the polygon. A single vertex becomes a Point part; zero vertices adds nothing.


### ddShapeSet::AddPoly()

```cpp
void AddPoly(const ddP *p, int n, bool closed, bool filled)
```

Adds a polygon / polyline from an array in one call (BeginPoly + PolyPush x n + EndPoly).


## ddPartEdges()

```cpp
inline int ddPartEdges(const ddShapePart &p)
```

**Returns:** Number of edges of a part: 1 for a segment; n-1 for an open polyline; n for a closed polygon of 3 or more vertices; 0 otherwise.

---
## ddPartEdge()

```cpp
inline void ddPartEdge(const ddShapeSet &s, const ddShapePart &p, int i, ddP &a, ddP &b)
```

Gets edge i of a part (0 <= i < [ddPartEdges](API-ddShapeSet#ddpartedges)).

| Parameter | Description |
|---|---|
| `a,b` | Outputs: the edge's end points. |

---
## ddPartRep()

```cpp
inline ddP ddPartRep(const ddShapeSet &s, const ddShapePart &p)
```

A point that lies on the part's outline (a circle's rightmost point, a polygon's first vertex ...). Used for containment tests.

---
## ddPartSolidContains()

```cpp
inline bool ddPartSolidContains(const ddShapeSet &s, const ddShapePart &p, const ddP &pt, bool solid, float tol)
```

Is a point inside the SOLID region of a part (a disc, or a filled closed polygon)?

| Parameter | Description |
|---|---|
| `solid` | Treat unfilled closed parts as solid too. |
| `tol` | Absolute tolerance (circles only). |

**Returns:** false for points, segments, open polylines and unfilled parts unless solid is true.

---
## struct ddEmit

```cpp
struct ddEmit
```

Collector for contact points: stores up to `max` points in a caller array and merges points closer than `tol`
(a polygon corner is hit by two edges but reported once). Internal helper of the contact tests.

---
### ddEmit::ddEmit()

```cpp
ddEmit(ddP *o, int m, float t)
```

| Parameter | Description |
|---|---|
| `o` | Output array. |
| `m` | Capacity of o. |
| `t` | Merge tolerance (points closer than this count as one). |


### ddEmit::Full()

```cpp
bool Full() const
```

**Returns:** true when the capacity is reached (callers stop searching).


### ddEmit::Add()

```cpp
void Add(const ddP &p)
```

Adds a point unless it is a near-duplicate or the collector is full.


## ddPartContacts()

```cpp
inline void ddPartContacts(const ddShapeSet &sa, const ddShapePart &a, const ddShapeSet &sb, const ddShapePart &b, ddEmit &em)
```

Boundary contact points between two parts (outline against outline), pushed into an [ddEmit](API-ddShapeSet#struct-ddemit).
Handles every pair of {point, circle rim, segment/polyline/polygon edges}. Parts are tested as OUTLINES only; for containment use [ddShapeSetsOverlap](API-ddShapeSet#ddshapesetsoverlap).

---
## ddShapeSetsContacts()

```cpp
inline int ddShapeSetsContacts(const ddShapeSet &sa, const ddShapeSet &sb, ddP *out, int maxOut, float tol = DD_SHAPE_TOL)
```

All boundary contact points between two shapes.

| Parameter | Description |
|---|---|
| `sa,sb` | The shapes (world space). |
| `out` | Array receiving the points. |
| `maxOut` | Capacity of out (the search stops when it is full). |
| `tol` | Merge / touch tolerance (default [DD_SHAPE_TOL](API-ddShapeSet#dd_shape_tol)). |

**Returns:** Number of points stored, 0..maxOut.

**Pitfalls**

- Pure containment has no boundary contact and returns 0; use [ddShapeSetsOverlap](API-ddShapeSet#ddshapesetsoverlap) for "do they overlap".

---
## ddShapeSetsOverlap()

```cpp
inline bool ddShapeSetsOverlap(const ddShapeSet &sa, const ddShapeSet &sb, bool solid = false, float tol = DD_SHAPE_TOL)
```

Do two shapes touch or overlap? True on any boundary contact, or when one lies wholly inside the other's solid region.

| Parameter | Description |
|---|---|
| `solid` | false (default): honour each part's Filled flag. true: treat every closed shape as filled. |
| `tol` | Touch tolerance (default [DD_SHAPE_TOL](API-ddShapeSet#dd_shape_tol)). |

**Tips and notes**

- Early-outs on the first hit, so it is cheaper than counting contacts.

---
