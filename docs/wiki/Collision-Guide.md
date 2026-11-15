# Collision guide

dd answers geometric questions at two levels:

| Level | Header | You give it | You get |
|---|---|---|---|
| **Geometry** | [ddGeom.h](API-ddGeom) | points, lines, circles, boxes | contact counts and points, distances, angles |
| **Scene shapes** | [ddCollide.h](API-ddCollide) | scene primitives (with their transforms) | overlap / contacts / "everything this touches" |

Geometry needs no graphics and no scene; you can use it on its own (it is plain C++ on floats).

## The rules (read this once)

1. **Return the count, write the points.** A collision function returns the **number of contact points** and writes them through optional pointer parameters, in order.
   With **0** contacts, **nothing is modified**; with **1**, only the first pointer is written. Pass `nullptr` for any output you do not need.
2. **`DD_INFINITE` (-1)** means infinitely many contacts (coincident shapes, collinear infinite lines). `if (n > 0)` = finite contacts; `if (n != 0)` = touching somehow.
3. **A "line" has an extent**: `ddExtent::Segment` (the finite piece A-B, the default), `Ray` (from A through B, onward), or `Line` (infinite). Contacts along it are in order of travel from A to B.
4. **A circle is its outline** in line/circle and circle/circle contact tests. A segment wholly inside a circle touches nothing. For "is this point inside the disc" use `ddCollidePointCircle`; for disc-vs-box use `ddCollideCircleBox`.
5. **A box is solid** for point/box, box/box and circle/box overlap tests, and an **outline** for `ddCollideLineBox`.
6. **Tolerances are relative to the shapes' local size.** Tangent and touching cases can come out either way; do not depend on exact tangency.

## Which function do I want?

| Question | Function |
|---|---|
| Is this point on that line (segment / ray / line), within a tolerance? | `ddCollidePointLine` |
| Is this point inside / on the rim of that circle? | `ddCollidePointCircle` (`outlineOnly` for the rim) |
| Is this point inside that box? | `ddCollidePointBox` |
| Is this point inside that polygon? | `ddPointInPolygon` |
| Where does a line cross a circle? | `ddCollideLineCircle` (0, 1 or 2 points, in travel order) |
| Where do two lines cross? | `ddCollideLineLine` (0 or 1 point; 2 for a collinear overlap) |
| Where do two circles' rims meet? | `ddCollideCircleCircle` |
| Where does a line enter and leave a box? | `ddCollideLineBox` |
| Do two boxes overlap? Where? | `ddCollideBoxBox` (overlap region optional) |
| Does a disc overlap a box? | `ddCollideCircleBox` |
| How far is a point from a line / box / circle? | `ddDistPointLine`, `ddDistPointBox`, `ddSignedDistPointCircle` |
| How far apart are two segments / circles? | `ddDistSegSeg`, `ddGapCircleCircle` (negative = overlap), `ddGapLineCircle` |
| Which side of a line is a point on? | `ddSide` |
| What is the angle between two directions? | `ddAngle`, `ddSignedAngle` |

## Worked examples

### A bullet against a wall and a target

```cpp
ddP bulletFrom(0, 0), bulletTo(100, 20);
ddP wallA(40, -50), wallB(40, 50);
ddP target(80, 16); float targetR = 6.0f;

ddP hit;
if (ddCollideLineLine(bulletFrom, bulletTo, wallA, wallB, &hit)) {
  // the bullet crosses the wall at `hit`
}
ddP e1, e2;
int n = ddCollideLineCircle(bulletFrom, bulletTo, target, targetR, &e1, &e2);
// n == 2: enters at e1, leaves at e2; n == 1: grazes; n == 0: misses (or the segment ends inside / before it)
```

### Line of sight with a ray

A **ray** from the viewer towards a point finds the first obstacle regardless of how far away it is:

```cpp
ddP p;
if (ddCollideLineCircle(eye, lookAt, obstacleCentre, obstacleR, &p, nullptr, ddExtent::Ray) > 0) {
  // p is the nearest hit along the ray (contacts are ordered from `eye`)
}
```

### Touch picking with a tolerance in pixels

```cpp
ddP w = r->ToWorld(ddP(touchX, touchY));
float tol = 6.0f / r->PixelsPerUnit();                  // 6 pixels in world units
ddVectorPrimitive *hit = scene.Pick(w, tol);
```

### Shapes in the scene

```cpp
ddCircle *a = scene.Emplace<ddCircle>(ddP(0, 0), 10.0f);               // an unfilled circle = a rim
ddRect   *b = scene.Emplace<ddRect>(ddP(0, 0), 8.0f, 8.0f);            // an unfilled square inside it

ddOverlap(a, b);          // false: the outlines never meet
ddOverlap(a, b, true);    // true:  solid = true treats both as occupying space
a->Filled = true;
ddOverlap(a, b);          // true:  a filled circle swallows the square
ddCollide(a, b);          // 0 in every case above: containment has no boundary contact
```

### Everything a moving object touches

```cpp
ddVectorPrimitive *touching[16];
int n = ddFindOverlaps(scene, player, touching, 16, /*solid=*/true);
for (int i = 0; i < n; i++) touching[i]->Color = TFT_RED;
```

`ddFindOverlaps` builds the probe's geometry once and rejects candidates by bounding box first, so it scales to large scenes. It excludes the probe itself and hidden objects.

## Scene shapes: how each primitive collides

| Primitive | Collision geometry |
|---|---|
| `ddPoint` | a filled disc of its radius |
| `ddLine` | a segment |
| `ddCircle`, `ddEllipse` | a true circle when the transform is a uniform scale (+ rotation); otherwise a 48-sided polygon. Filled = disc. |
| `ddRect`, `ddNGon` | a closed polygon (solid when Filled) |
| `ddPolygon` | a closed polygon or open polyline (solid when Filled and closed) |
| `ddArrow` | the shaft (segment) plus the head (triangle, solid when Filled; otherwise two segments) |
| `ddArc` | a polyline; a Pie is a closed polygon, solid when Filled |
| `ddBezier` | a 24-segment polyline |
| `ddSprite` | its rectangle (solid) |
| `ddMarker` | a single point (its pixel size is not known to the world) |
| `ddGroup` | all its visible children, with the group's transform |
| `ddText`, `ddGrid` | none |

Hidden primitives (`Visible = false`) have no collision geometry.

## Pitfalls

- **"They overlap but `ddCollide` says 0."** Containment has no boundary contact. Use `ddOverlap`, and remember unfilled shapes are outlines (use `Filled` or `solid = true`).
- **"The line is inside the circle but `ddCollideLineCircle` says 0."** A circle is its rim there. Use `ddGapLineCircle(...) < 0` or test the end points with `ddCollidePointCircle`.
- **Nested circles** (one inside the other, not touching) return 0 contacts from `ddCollideCircleCircle`. Use `ddGapCircleCircle(...) < 0` for disc overlap.
- **Tangent and just-touching cases** can return 0 or 1 depending on rounding. Give yourself margin (pass a small positive `tol`) if you need "touching counts".
- **Almost-parallel lines** (sine of the angle below about 1e-5) are treated as parallel by `ddCollideLineLine`.
- **Non-uniformly scaled circles** collide as a 48-gon: accurate to about 0.2 % of the radius, not exact.
- **Thread safety:** the shape-level functions share static scratch buffers; do not call them from two threads or recursively.
- **A box is axis-aligned.** A rotated rectangle in the scene is a polygon (exact); `ddBox` is for broad-phase and for genuinely axis-aligned things.
