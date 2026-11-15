# ddCollide.h

*Shape-to-shape collision.*  Source: [`src/ddCollide.h`](https://github.com/JasonBolbach/dd/blob/main/src/ddCollide.h)

Primitives are tested where they are DRAWN: their transform (and group nesting) is applied. Both primitives are taken to be top-level
(parent = world) unless you pass their parent matrices.

**Semantics**

- An unfilled shape is only its outline: a small unfilled circle inside a big one does not touch it. A filled shape is solid. Pass solid = true to treat every closed shape as filled.
- Contacts are BOUNDARY points. A shape wholly inside another has none: use [ddOverlap](API-ddCollide#ddoverlap) for that.
- Hidden (Visible = false) primitives have no collision geometry. [ddText](API-ddVector#struct-ddtext) and [ddGrid](API-ddVector#struct-ddgrid) never collide.
- Circles under a non-uniform scale or skew, arcs and Beziers are tested as polylines (an approximation).

```cpp
bool hit = ddOverlap(a, b);                        // do they touch or overlap?
ddP p1, p2;
int  n   = ddCollide(a, b, &p1, &p2);              // number of boundary contact points; the first two returned
ddP  pts[16];
int  m   = ddContacts(a, b, pts, 16);              // ...or all of them
ddVectorPrimitive *found[8];
int  k   = ddFindOverlaps(scene, a, found, 8);     // everything in the scene that a touches
```

**Pitfalls**

- NOT thread-safe: the functions share two static scratch ShapeSets.
- The scratch sets are cleared and refilled on every call, so these functions are not re-entrant either (do not call them from inside a primitive's Shapes()).

**Tips and notes**

- For many objects against one, use [ddFindOverlaps](API-ddCollide#ddfindoverlaps): it builds the probe's geometry once and rejects with bounding boxes first.

## Contents

- [ddScratchShapes()](#ddscratchshapes)
- [ddOverlap()](#ddoverlap)
- [ddContacts()](#ddcontacts)
- [ddCollide()](#ddcollide)
- [ddFindOverlaps()](#ddfindoverlaps)

## ddScratchShapes()

```cpp
inline ddShapeSet &ddScratchShapes(int i)
```

The two shared scratch shape sets used by the functions below (index 0 and 1). Exposed for advanced use; do not hold references across calls.

---
## ddOverlap()

```cpp
inline bool ddOverlap(ddVectorPrimitive *a, ddVectorPrimitive *b, bool solid = false, const Matrix3x3 &parentA = Matrix3x3(), const Matrix3x3 &parentB = Matrix3x3())
```

Do two primitives touch or overlap?

| Parameter | Description |
|---|---|
| `a,b` | The primitives. |
| `solid` | false (default): filled shapes are solid, unfilled ones are outlines. true: every closed shape counts as solid. |
| `parentA,parentB` | World-from-parent matrices (identity for top-level primitives). |

**Returns:** true on any boundary contact or if one lies inside the other's solid region. False if either pointer is null or hidden.

**Tips and notes**

- The cheapest of the collision calls (stops at the first hit).

---
## ddContacts()

```cpp
inline int ddContacts(ddVectorPrimitive *a, ddVectorPrimitive *b, ddP *out, int maxOut, bool solid = false, const Matrix3x3 &parentA = Matrix3x3(), const Matrix3x3 &parentB = Matrix3x3())
```

Boundary contact points between two primitives.

| Parameter | Description |
|---|---|
| `out` | Array receiving the points. |
| `maxOut` | Capacity of out. |
| `solid` | Accepted for symmetry with [ddOverlap](API-ddCollide#ddoverlap); contact points are boundary points either way. |

**Returns:** Number of distinct points stored (0..maxOut).

**Pitfalls**

- A shape wholly inside another gives 0 here although [ddOverlap](API-ddCollide#ddoverlap) would say true.

---
## ddCollide()

```cpp
inline int ddCollide(ddVectorPrimitive *a, ddVectorPrimitive *b, ddP *p1 = nullptr, ddP *p2 = nullptr, bool solid = false, const Matrix3x3 &parentA = Matrix3x3(), const Matrix3x3 &parentB = Matrix3x3())
```

Boundary contact count, with the first two points returned through pointers (the convention of ddGeom.h).

| Parameter | Description |
|---|---|
| `p1,p2` | Optional outputs for the first two contacts. Untouched when there are fewer. |

**Returns:** The number of contact points found, counted up to 64 (so a result above 2 means more contacts than the two returned).

**Tips and notes**

- Use [ddContacts](API-ddCollide#ddcontacts) if you need all of the points.

---
## ddFindOverlaps()

```cpp
inline int ddFindOverlaps(RenderBlock &scene, ddVectorPrimitive *p, ddVectorPrimitive **out, int maxOut, bool solid = false, const Matrix3x3 &parent = Matrix3x3())
```

Finds every visible top-level primitive of a scene that overlaps a probe primitive.

| Parameter | Description |
|---|---|
| `scene` | The scene to search. |
| `p` | The probe (excluded from the results). |
| `out` | Array of pointers receiving the matches. |
| `maxOut` | Capacity of out (the search stops when it is full). |
| `solid` | As for [ddOverlap](API-ddCollide#ddoverlap). |
| `parent` | World-from-parent matrix of the probe (identity if it is top-level). |

**Returns:** Number of matches stored.

**Tips and notes**

- Candidates are first rejected by bounding box (ParentBounds), so it scales well to large scenes. Primitives without bounds are always tested.

---
