# ddVector.h

*Scene and primitives.*  Source: [`src/ddVector.h`](https://github.com/JasonBolbach/dd/blob/main/src/ddVector.h)

A scene is a [RenderBlock](API-ddVector#class-renderblock) holding [ddVectorPrimitive](API-ddVector#struct-ddvectorprimitive) objects (circles, lines, polygons, groups ...) that live in an arena.
You create them with `scene.Emplace<ddCircle>(...)`, keep the returned pointer, change their properties whenever you like
(transform, colour, shape fields, visibility) and call `scene.Draw(r)` once per frame.

Every primitive has:

- its own transform (SetPos / SetAngle / SetScale / Translate / Transform());
- a Visible flag (a hidden primitive costs nothing: no transform, no culling test, no draw);
- automatic culling: primitives with known bounds that are off-screen are skipped;
- a Pen (width / dashed / dotted) for line-based shapes;
- shape-specific public fields you can change at any time;
- HitTest() for picking, CollectShapes() for collision, plus Draggable / Tag for the interaction layer ([ddInteract](API-ddInteract#class-ddinteract).h).

**Geometry and position**

Geometry is defined around the primitive's LOCAL origin and positioned by its transform. Constructors that take a
centre or position ([ddCircle](API-ddVector#struct-ddcircle)(c, r) ...) place the primitive for you by setting its position, so rotating or scaling it
later pivots around that centre, not around the world origin.

**Lifetime**

Primitives live in a [ddArena](API-ddArena#class-ddarena). NEVER `delete` one. Use [RenderBlock](API-ddVector#class-renderblock)::Remove / Clear / Reset. A pointer you hold is valid
until you Remove that primitive or Clear/Reset its scene.

**Pitfalls**

- A primitive class must derive SINGLY from [ddVectorPrimitive](API-ddVector#struct-ddvectorprimitive) (no multiple inheritance), so that the object's address equals the arena block's address.

**Tips and notes**

- To add your own primitive: derive from [ddVectorPrimitive](API-ddVector#struct-ddvectorprimitive), implement DrawShape (and optionally HitLocal, LocalBounds, Shapes), and create it with `scene.Emplace<YourType>(...)`.

## Contents

- [ddMeanScale()](#ddmeanscale)
- [ddSegDist()](#ddsegdist)
- [ddInPoly()](#ddinpoly)
- [ddAutoSteps()](#ddautosteps)
- [ddBoxThrough()](#ddboxthrough)
- [ddIsSimilarity()](#ddissimilarity)
- [ddAddUnitCircleShape()](#ddaddunitcircleshape)
- [struct ddVectorPrimitive](#struct-ddvectorprimitive)
- [class RenderBlock](#class-renderblock)
- [struct ddPoint](#struct-ddpoint)
- [struct ddLine](#struct-ddline)
- [ddDrawUnitCircle()](#dddrawunitcircle)
- [struct ddCircle](#struct-ddcircle)
- [struct ddEllipse](#struct-ddellipse)
- [struct ddRect](#struct-ddrect)
- [struct ddPolygon](#struct-ddpolygon)
- [struct ddNGon](#struct-ddngon)
- [struct ddArrow](#struct-ddarrow)
- [enum ddMarkerType](#enum-ddmarkertype)
- [struct ddMarker](#struct-ddmarker)
- [struct ddArc](#struct-ddarc)
- [struct ddBezier](#struct-ddbezier)
- [struct ddSprite](#struct-ddsprite)
- [struct ddText](#struct-ddtext)
- [struct ddGrid](#struct-ddgrid)
- [class ddGroup](#class-ddgroup)

## ddMeanScale()

```cpp
inline float ddMeanScale(const Matrix3x3 &m)
```

The "average" scale of a matrix: sqrt(|determinant of its 2x2 block|). For a uniform scale it is that scale exactly.

**Returns:** Scale factor >= 0. Used to convert a world radius into a size under a transform.

**Pitfalls**

- For a strongly non-uniform scale it is only an approximation (the geometric mean of the two axis scales).

---
## ddSegDist()

```cpp
inline float ddSegDist(const ddP &p, const ddP &a, const ddP &b)
```

Distance from a point to a segment. Thin wrapper over [ddDistPointLine](API-ddGeom#dddistpointline) (kept for the shapes' hit tests).

---
## ddInPoly()

```cpp
inline bool ddInPoly(const ddP &p, const ddP *v, int n)
```

Even-odd point-in-polygon. Thin wrapper over [ddPointInPolygon](API-ddGeom#ddpointinpolygon) (kept for the shapes' hit tests).

---
## ddAutoSteps()

```cpp
inline int ddAutoSteps(float pxLength, int lo = 12, int hi = 180)
```

Chooses how many segments to use for a curve from its on-screen length: about one segment per 4 pixels, clamped.

| Parameter | Description |
|---|---|
| `pxLength` | Approximate on-screen length of the curve in pixels. |
| `lo,hi` | Clamp range for the result (defaults 12..180). |

**Returns:** Segment count. Used by circles, arcs and Beziers so small curves are cheap and large ones are smooth.

---
## ddBoxThrough()

```cpp
inline void ddBoxThrough(const Matrix3x3 &m, float x0, float y0, float x1, float y1, float &ox0, float &oy0, float &ox1, float &oy1)
```

Axis-aligned bounding box of a box pushed through a matrix (transform the four corners, take min/max).

| Parameter | Description |
|---|---|
| `m` | The transform. |
| `x0,y0,x1,y1` | The input box. |
| `ox0,oy0,ox1,oy1` | Output: the world-space box. Grows when m rotates (a rotated box's AABB is bigger). |

---
## ddIsSimilarity()

```cpp
inline bool ddIsSimilarity(const Matrix3x3 &m, float &s)
```

Is the matrix a similarity transform: uniform scale combined with any rotation or reflection (no skew, no non-uniform scale)?

| Parameter | Description |
|---|---|
| `s` | Output: the uniform scale factor (valid when the result is true). |

**Returns:** true if circles stay circles under this matrix.

---
## ddAddUnitCircleShape()

```cpp
inline void ddAddUnitCircleShape(const Matrix3x3 &m, ddShapeSet &out, bool filled, int steps = 48)
```

Adds the unit circle pushed through matrix m to a collision shape set.
A true circle part when m is a similarity; otherwise (non-uniform scale / skew produces an ellipse) a closed polygon approximation.

| Parameter | Description |
|---|---|
| `filled` | Disc (true) or rim only (false). |
| `steps` | Polygon segments for the ellipse case (default 48). |

**Tips and notes**

- Collision against a non-uniformly scaled circle is therefore an approximation accurate to roughly 1 - cos(PI/48) of the radius.

---
## struct ddVectorPrimitive

```cpp
struct ddVectorPrimitive
```

Base class of everything you can put in a scene.

Holds the state common to all primitives: colour, visibility, a pen, an application Tag, a Draggable flag and the
transform. Provides the public operations (Render, HitTest, CollectShapes, ParentBounds) in terms of virtual hooks that
each shape implements: DrawShape, HitLocal, LocalBounds, Shapes.

**Public fields**

- `Color`     RGB565 colour (TFT_* constants or lcd.color565()). Default white.
- `Visible`   false = completely skipped by Draw, HitTest/Pick and collision. Use it instead of Remove for things that come back.
- `Draggable` true = [ddInteract](API-ddInteract#class-ddinteract) moves it when dragged.
- `Tag`       Free for your use (button ids, object kinds ...). Not interpreted by the library.
- `Pen`       Width / style of line-based shapes (see [ddPen](API-ddRender#struct-ddpen)).

**Pitfalls**

- Do not store a primitive by value or copy one: it is arena-allocated and linked into a list.
- Changing a transform through SetPos/SetAngle/SetScale/Translate is cheap and lazy; the matrix is rebuilt once at the next Render / HitTest / bounds query.

---
### ddVectorPrimitive::SetPos()

```cpp
void SetPos(float x, float y)
```

Sets the primitive's position (the translation of its transform, in its PARENT's space - world space for top-level primitives).

**Tips and notes**

- The primitive pivots (rotates / scales) about its local origin, which is where it was placed.


### ddVectorPrimitive::SetPos() (overload 2)

```cpp
void SetPos(const ddP &p)
```

Same as SetPos(x, y) with a point.


### ddVectorPrimitive::SetAngle()

```cpp
void SetAngle(float radians)
```

Sets the rotation about the local origin.

| Parameter | Description |
|---|---|
| `radians` | Counter-clockwise (y up). Use DDD_DEG2RAD for degrees. |

**Tips and notes**

- This is ABSOLUTE (it replaces the previous angle). To spin continuously, keep your own angle variable, or read GetTransform().Angle and add to it.


### ddVectorPrimitive::SetScale()

```cpp
void SetScale(float x, float y)
```

Sets the scale about the local origin (absolute, replaces the previous scale).

**Pitfalls**

- Non-uniform scale on a circle turns it into an ellipse (drawn and collided as a polygon). Negative values mirror.


### ddVectorPrimitive::SetScale() (overload 2)

```cpp
void SetScale(float s)
```

Uniform scale (same factor on both axes).


### ddVectorPrimitive::Translate()

```cpp
void Translate(float dx, float dy)
```

Moves the primitive by an offset (relative; adds to the current position). Used by dragging.


### ddVectorPrimitive::Transform()

```cpp
ddT &Transform()
```

Direct access to the transform for bulk changes (Tx, Ty, Angle, Sx, Sy).

**Pitfalls**

- Calling Transform() marks the transform dirty at that moment. If you keep the reference and modify it later without calling Transform() again, the change is not picked up until something else marks it dirty. Fetch it each time.


### ddVectorPrimitive::GetTransform()

```cpp
const ddT &GetTransform() const
```

Read-only access to the transform (position `Tx,Ty`, `Angle`, scale `Sx,Sy`). Does not mark anything dirty.

**Tips and notes**

- The stored components are always current even if the matrix `M` has not been rebuilt yet.


### ddVectorPrimitive::Render()

```cpp
void Render(const Matrix3x3 &parent, ddR *r)
```

Draws the primitive (called by [RenderBlock](API-ddVector#class-renderblock)::Draw).
Hidden primitives do nothing. Otherwise: combine the transform with `parent`, cull if its bounds are off-screen, then DrawShape.

| Parameter | Description |
|---|---|
| `parent` | World-from-parent matrix (identity for top-level primitives; a group passes its own matrix to its children). |
| `r` | The renderer. |

**Tips and notes**

- You normally never call this directly.


### ddVectorPrimitive::HitTest()

```cpp
bool HitTest(const ddP &p, float tol = 0.0f)
```

Is a point on/inside this primitive?

| Parameter | Description |
|---|---|
| `p` | The point in the PARENT's space (world space for top-level primitives; [ddGroup](API-ddVector#class-ddgroup) tests its children this way). |
| `tol` | Slop in the same units, e.g. a few pixels converted with 1 / PixelsPerUnit(). Default 0. |

**Returns:** true on a hit. Filled shapes hit anywhere inside; unfilled shapes only near their outline.

**Pitfalls**

- Hidden primitives are NOT excluded here ([RenderBlock](API-ddVector#class-renderblock)::Pick skips them). Check Visible yourself if you call HitTest directly.


### ddVectorPrimitive::CollectShapes()

```cpp
void CollectShapes(const Matrix3x3 &parent, ddShapeSet &out)
```

Appends this primitive's collision geometry, in WORLD space, to a shape set. Hidden primitives add nothing.

| Parameter | Description |
|---|---|
| `parent` | World-from-parent matrix. |
| `out` | The set to append to (not cleared). |

**Tips and notes**

- This is what [ddOverlap](API-ddCollide#ddoverlap) / [ddCollide](API-ddCollide#ddcollide) / [ddFindOverlaps](API-ddCollide#ddfindoverlaps) are built on. Call it yourself to build custom collision queries.


### ddVectorPrimitive::ParentBounds()

```cpp
bool ParentBounds(float &x0, float &y0, float &x1, float &y1)
```

Axis-aligned bounds of the primitive in its PARENT's space (world space for top-level primitives), including its transform.

| Parameter | Description |
|---|---|
| `x0,y0,x1,y1` | Outputs: min/max corners. |

**Returns:** false if the shape has no known bounds (text, markers) - such shapes are never culled.

**Tips and notes**

- A rotated primitive's AABB is larger than the shape itself; use it for broad-phase rejection only.


### ddVectorPrimitive::Touch()

```cpp
void Touch()
```

Marks the transform as in use and needing a rebuild. Called by every setter.


### ddVectorPrimitive::DrawShape()

```cpp
virtual void DrawShape(const Matrix3x3 &m, ddR *r) = 0
```

Hook: draw the shape. Implemented by every primitive.

| Parameter | Description |
|---|---|
| `m` | World-from-local matrix (parent chain and this primitive's transform already combined). |
| `r` | The renderer (use r->W2D * m to go from local coordinates straight to display pixels). |


### ddVectorPrimitive::HitLocal()

```cpp
virtual bool HitLocal(const ddP &, float)
```

Hook: hit test in the primitive's LOCAL space. The base class converts the incoming point and tolerance
(dividing the tolerance by the mean scale). Default: never hit.


### ddVectorPrimitive::Shapes()

```cpp
virtual void Shapes(const Matrix3x3 &, ddShapeSet &)
```

Hook: add collision geometry in WORLD space. Default: none (the primitive does not collide).

**Tips and notes**

- Circles that stay circles, polygons, polylines, segments and points are produced; see [ddShapeSet](API-ddShapeSet#class-ddshapeset).h.


### ddVectorPrimitive::LocalBounds()

```cpp
virtual bool LocalBounds(float &, float &, float &, float &)
```

Hook: local-space bounding box, used for culling and ParentBounds. Return false for "unknown / unbounded" (never culled).


## class RenderBlock

```cpp
class RenderBlock
```

The scene: an ordered list of primitives backed by an arena. Draw order is insertion order (the last added is on top).

A top-level [RenderBlock](API-ddVector#class-renderblock) OWNS an arena that grows on demand up to a byte cap. A [RenderBlock](API-ddVector#class-renderblock) built from an existing arena (this is how
[ddGroup](API-ddVector#class-ddgroup) holds its children) shares it.

Two usage styles are supported and can be mixed:

- RETAINED: build the scene once, then modify objects each frame (cheapest per frame).
- PER-FRAME: `scene.Clear()` then re-Emplace everything each frame. Clear() on an owning block keeps its chunks, so this does no malloc/free.

```cpp
RenderBlock scene;                                   // default cap 256 KB
ddCircle *c = scene.Emplace<ddCircle>(ddP(0, 0), 10.0f, TFT_CYAN, true);
if (!c) handleFull();                                // arena cap reached
c->SetPos(5, 5);
scene.Draw(&renderer);
scene.Remove(c);                                     // c is invalid from here on
scene.Clear();                                       // remove everything, keep the memory
```

**Pitfalls**

- Emplace returns nullptr when the arena cap is reached. Check it (or size the cap generously and watch Arena()->Overflowed()).
- Not thread-safe.
- After Remove(p), Clear() or Reset(), every pointer to the affected primitives is dangling. If you also keep a copy in an interaction layer or your own list, drop it first ([ddInteract](API-ddInteract#class-ddinteract)::Forget).

---
### RenderBlock::RenderBlock()

```cpp
explicit RenderBlock(size_t maxBytes = DD_ARENA_DEFAULT_MAX)
```

Creates a scene that owns its own arena.

| Parameter | Description |
|---|---|
| `maxBytes` | Byte cap for the arena (default [DD_ARENA_DEFAULT_MAX](API-ddArena#dd_arena_default_max) = 256 KB). Size it from Arena()->Peak() of your heaviest scene. |


### RenderBlock::RenderBlock() (overload 2)

```cpp
explicit RenderBlock(ddArena *shared)
```

Creates a block that allocates from an existing arena (used by [ddGroup](API-ddVector#class-ddgroup)). Clear() on such a block destroys its primitives
individually instead of resetting the whole arena.


### RenderBlock::RenderBlock() (overload 3)

```cpp
RenderBlock(const RenderBlock &) = delete
```

Non-copyable.


### RenderBlock::Arena()

```cpp
ddArena *Arena()
```

**Returns:** The arena in use (for statistics: InUse, Peak, Capacity, Overflowed).


### RenderBlock::Emplace()

```cpp
template <class T, class... Args> T *Emplace(Args &&...args)
```

Creates a primitive in the arena and adds it on top of the draw order.

| Parameter | Description |
|---|---|
| `T` | A class derived from [ddVectorPrimitive](API-ddVector#struct-ddvectorprimitive) ([ddCircle](API-ddVector#struct-ddcircle), [ddRect](API-ddVector#struct-ddrect) ... or your own). |
| `args` | The constructor arguments of T. |

**Returns:** Pointer to the new primitive, or nullptr if the arena cap was reached.

**Pitfalls**

- Check for nullptr. Keep the pointer if you want to modify, hide or remove the primitive later.


### RenderBlock::EmplacePolygon()

```cpp
ddPolygon *EmplacePolygon(const ddP *pts, int n, uint16_t color = TFT_WHITE, bool closed = true, bool filled = false)
```

Creates a polygon or polyline whose points are COPIED into the arena, so your source array can be a temporary.

| Parameter | Description |
|---|---|
| `pts` | Vertices in local coordinates. |
| `n` | Vertex count. |
| `closed` | true = polygon (last vertex connects to the first), false = open polyline. |
| `filled` | true = solid (needs `closed`). |

**Returns:** The polygon, or nullptr if the arena is full. The copied points are freed with the polygon.

**Tips and notes**

- Prefer this over constructing a [ddPolygon](API-ddVector#struct-ddpolygon) with a pointer you must keep alive yourself.


### RenderBlock::EmplaceGroup()

```cpp
ddGroup *EmplaceGroup()
```

Creates an empty group (a primitive with its own transform that contains child primitives).

**Returns:** The group, or nullptr if the arena is full. Add children with `group->Children.Emplace<...>(...)`.


### RenderBlock::Detach()

```cpp
void Detach(ddVectorPrimitive *p)
```

Unlinks a primitive from the draw list WITHOUT destroying it. The pointer stays valid and its memory is reclaimed at the next
Clear() / Reset(). Rarely needed; Remove() is usually what you want.


### RenderBlock::Remove()

```cpp
void Remove(ddVectorPrimitive *p)
```

Unlinks, destroys and recycles a primitive. For a group this also destroys all its children.

**Pitfalls**

- `p` is invalid afterwards. Do not call it twice for the same pointer.


### RenderBlock::Clear()

```cpp
void Clear()
```

Removes every primitive.
An owning block resets its arena (all chunks are kept, so this is cheap and does not free memory to the system). A block that shares an arena
destroys and recycles each of its primitives individually.


### RenderBlock::Reset()

```cpp
void Reset()
```

Alias for Clear().


### RenderBlock::MoveToFront()

```cpp
void MoveToFront(ddVectorPrimitive *p)
```

Moves a primitive to the top of the draw order (drawn last, picked first).


### RenderBlock::MoveToBack()

```cpp
void MoveToBack(ddVectorPrimitive *p)
```

Moves a primitive to the bottom of the draw order (drawn first, picked last).


### RenderBlock::Count()

```cpp
int Count() const
```

**Returns:** Number of top-level primitives in this block (children of groups are counted by their own blocks).


### RenderBlock::First()

```cpp
ddVectorPrimitive *First() const
```

**Returns:** The first (bottom) primitive, or nullptr. Together with Next() lets you iterate the block.


### RenderBlock::Next()

```cpp
ddVectorPrimitive *Next(const ddVectorPrimitive *p) const
```

**Returns:** The primitive drawn after p, or nullptr at the end.


### RenderBlock::Draw()

```cpp
void Draw(ddR *r, const Matrix3x3 &parent = Matrix3x3())
```

Draws every primitive in order, applying transforms and culling.

| Parameter | Description |
|---|---|
| `r` | The renderer. |
| `parent` | World-from-parent matrix (default identity). A [ddGroup](API-ddVector#class-ddgroup) passes its own transform here for its children. |

**Tips and notes**

- Call r->ResetStats() before this if you want per-frame Drawn / Culled counts.


### RenderBlock::Pick()

```cpp
ddVectorPrimitive *Pick(const ddP &p, float tol = 0.0f)
```

Finds the topmost visible primitive under a point.

| Parameter | Description |
|---|---|
| `p` | The point in world space (e.g. r->ToWorld(touchPixel)). |
| `tol` | Slop in world units (convert pixels with 1 / r->PixelsPerUnit()). |

**Returns:** The primitive, or nullptr. Returns top-level primitives (a hit on a group's child returns the group).

**Tips and notes**

- Iterates from the top of the draw order, so overlapping objects are resolved visually.


### RenderBlock::Bounds()

```cpp
bool Bounds(float &x0, float &y0, float &x1, float &y1)
```

Union of the bounds of the visible primitives, in this block's space.

**Returns:** false if the block is empty or ANY visible child has unknown bounds.


## struct ddPoint

```cpp
struct ddPoint : ddVectorPrimitive
```

A filled dot.

**Fields**

- `Radius` in WORLD units (scales with the transform and the world). `Label` draws the world coordinates above it (needs DD_DEBUG).

**Behaviour**

Draws as a filled disc. Hit-tests and collides as a filled disc of that radius.

**Pitfalls**

- A very small radius can round to 0 pixels and vanish; use [ddMarker](API-ddVector#struct-ddmarker) for a fixed pixel-sized mark.

---
### ddPoint::ddPoint()

```cpp
ddPoint(ddP p, float rad = 1, uint16_t color = TFT_WHITE, bool label = false)
```

| Parameter | Description |
|---|---|
| `p` | Position (world units). |
| `rad` | Radius in world units (default 1). |
| `color` | RGB565 colour (default white). |
| `label` | Draw the coordinates label (only with DD_DEBUG). |


## struct ddLine

```cpp
struct ddLine : ddVectorPrimitive
```

A line segment from `A` to `B`, both in local coordinates.

**Fields**

- `A`, `B` end points. Use `Pen` for width and dashes.

**Behaviour**

Hit-tests near the segment; collides as a segment.

**Tips and notes**

- The constructor does NOT move the primitive: A and B are interpreted in the parent's space until you give it a transform.

---
### ddLine::ddLine()

```cpp
ddLine(ddP a, ddP b, uint16_t color = TFT_WHITE)
```

| Parameter | Description |
|---|---|
| `a,b` | End points. |
| `color` | RGB565 colour (default white). |


## ddDrawUnitCircle()

```cpp
inline void ddDrawUnitCircle(const Matrix3x3 &m, ddR *r, uint16_t color, int steps, bool filled, const ddPen &pen)
```

Draws the unit circle pushed through a matrix. The shared drawing code of [ddCircle](API-ddVector#struct-ddcircle) and [ddEllipse](API-ddVector#struct-ddellipse).
Uses LovyanGFX's circle/ellipse primitives when the result is axis-aligned on screen and the pen is plain (or the shape is filled); otherwise
a polyline whose segment count follows the on-screen size, which also honours the pen.

| Parameter | Description |
|---|---|
| `m` | World-from-unit-circle matrix (already includes the radius scale). |
| `steps` | Fallback segment count; <= 0 means automatic. |
| `filled` | Fill instead of outline. |

---
## struct ddCircle

```cpp
struct ddCircle : ddVectorPrimitive
```

A circle (outline or disc).

**Fields**

- `Radius` world units. `Filled` disc instead of rim. `Steps` fallback polygon segments (0 = automatic).

**Behaviour**

Drawn with LovyanGFX's fast circle when the transform keeps it round and the pen is plain; otherwise as a polyline. Under a NON-UNIFORM
scale it becomes an ellipse. Collides as a true circle when the transform is a similarity, otherwise as a 48-sided polygon.

**Pitfalls**

- An unfilled circle is only its rim for hit-testing and collision: a point or small shape inside it does not touch it. Use `Filled = true`, or pass solid = true to the collision functions, to treat it as a disc.

---
### ddCircle::ddCircle()

```cpp
ddCircle(ddP c, float radius, uint16_t color = TFT_WHITE, bool filled = false, int steps = 0)
```

| Parameter | Description |
|---|---|
| `c` | Centre (world units); sets the position. |
| `radius` | World units. |
| `color` | RGB565 colour (default white). |
| `filled` | Disc (true) or rim (false, default). |
| `steps` | Fallback segment count (0 = automatic). |


## struct ddEllipse

```cpp
struct ddEllipse : ddVectorPrimitive
```

An axis-aligned ellipse in local space; rotate it with SetAngle().

**Fields**

- `Rx`, `Ry` radii in world units. `Filled`, `Steps` as for [ddCircle](API-ddVector#struct-ddcircle).

**Behaviour**

Fast LovyanGFX ellipse when axis-aligned on screen with a plain pen; polyline otherwise (always the case once rotated by an angle that is not a multiple of 90 degrees).

---
### ddEllipse::ddEllipse()

```cpp
ddEllipse(ddP c, float rx, float ry, uint16_t color = TFT_WHITE, bool filled = false, int steps = 0)
```

| Parameter | Description |
|---|---|
| `c` | Centre (world units). |
| `rx,ry` | Radii in world units. |
| `color` | RGB565 colour. |
| `filled` | Fill. |
| `steps` | Fallback segments (0 = automatic). |


## struct ddRect

```cpp
struct ddRect : ddVectorPrimitive
```

A rectangle centred on its local origin (rotate it with SetAngle).

**Fields**

- `W`, `H` width and height in world units. `Filled`.

**Behaviour**

Axis-aligned on screen (including 90-degree turns) with a plain pen: LovyanGFX drawRect/fillRect. Otherwise drawn as a 4-point polygon.
Collides as a (rotated) quadrilateral.

**Pitfalls**

- Positioned by its CENTRE, not its top-left corner.

---
### ddRect::ddRect()

```cpp
ddRect(ddP c, float w, float h, uint16_t color = TFT_WHITE, bool filled = false)
```

| Parameter | Description |
|---|---|
| `c` | Centre (world units). |
| `w,h` | Width and height in world units. |
| `color` | RGB565 colour. |
| `filled` | Fill. |


## struct ddPolygon

```cpp
struct ddPolygon : ddVectorPrimitive
```

A polygon (closed) or polyline (open) through a list of points in local coordinates.

**Fields**

- `Pts`, `Count` the vertices. `Closed` joins the last to the first. `Filled` fills (concave allowed; even-odd). `Owner` is set when the points live in the arena.

**Behaviour**

Filled polygons use a scanline fill that handles concave outlines. Hit-tests inside (when filled) or near the edges. Collides as a polygon / polyline.

**Pitfalls**

- `Pts` is NOT copied by the constructor. The array must outlive the primitive. Use [RenderBlock](API-ddVector#class-renderblock)::EmplacePolygon to have it copied into the arena instead.
- Bounds are recomputed from the points every frame (O(n)); very large polygons that never change are better kept as a few shapes.

**Tips and notes**

- You may edit the points in place between frames; the polygon reads them each draw.

---
### ddPolygon::ddPolygon()

```cpp
ddPolygon(const ddP *pts, int n, uint16_t color = TFT_WHITE, bool closed = true, bool filled = false)
```

| Parameter | Description |
|---|---|
| `pts` | Vertices (not copied). |
| `n` | Vertex count. |
| `color` | RGB565 colour. |
| `closed` | true = polygon. |
| `filled` | true = solid. |


### ddPolygon::~ddPolygon()

```cpp
~ddPolygon()
```

Frees the point array if it was allocated by EmplacePolygon (Owner set).


## struct ddNGon

```cpp
struct ddNGon : ddVectorPrimitive
```

A regular polygon (triangle, square, hexagon ...) with no point storage; vertices are generated on the fly.

**Fields**

- `Sides` (>= 3), `Radius` circumradius in world units, `Phase` angle of the first vertex in radians, `Filled`.

**Behaviour**

Drawn as a polygon; collides as a polygon. Cheap in memory (a few floats).

---
### ddNGon::ddNGon()

```cpp
ddNGon(ddP c, int sides, float radius, uint16_t color = TFT_WHITE, bool filled = false, float phase = 0.0f)
```

| Parameter | Description |
|---|---|
| `c` | Centre (world units). |
| `sides` | Number of sides (values below 3 are raised to 3). |
| `radius` | Circumradius in world units. |
| `color` | RGB565 colour. |
| `filled` | Fill. |
| `phase` | Rotation of the first vertex in radians. |


## struct ddArrow

```cpp
struct ddArrow : ddVectorPrimitive
```

An arrow: a shaft from `A` (tail) to `B` (tip) with a head at B.

**Fields**

- `A`, `B` local end points. `HeadLength`, `HeadWidth` in world units. `Filled` = solid triangular head (shaft stops at its base); false = open "V" head.

**Behaviour**

The head is built in local space so it follows the transform. Hit-tests along the shaft. Collides as the shaft plus the head.

**Pitfalls**

- A zero-length arrow draws nothing. The head length is clamped to the arrow's length.

---
### ddArrow::ddArrow()

```cpp
ddArrow(ddP a, ddP b, uint16_t color = TFT_WHITE, float headLength = 6.0f, float headWidth = 5.0f, bool filled = true)
```

| Parameter | Description |
|---|---|
| `a,b` | Tail and tip. |
| `color` | RGB565 colour. |
| `headLength` | World units along the shaft. |
| `headWidth` | World units across. |
| `filled` | Solid head. |


## enum ddMarkerType

```cpp
enum struct ddMarkerType
```

Shape of a [ddMarker](API-ddVector#struct-ddmarker): a plus, an X, a diamond, a square or a circle.

---
## struct ddMarker

```cpp
struct ddMarker : ddVectorPrimitive
```

A fixed PIXEL-size mark at its local origin: it does not grow when the world is scaled. Good for plotted data points and cursors.

**Fields**

- `Type` ([ddMarkerType](API-ddVector#enum-ddmarkertype)), `SizePx` half-size in pixels, `Filled` for the closed shapes.

**Behaviour**

Has no bounds, so it is never culled. Hit-tests only within the tolerance of its position, and collides as a single point (its pixel size is
unknown to the world-space code).

**Pitfalls**

- Because its bounds are unknown, thousands of markers are drawn even when off-screen. Hide or remove those you do not need.

---
### ddMarker::ddMarker()

```cpp
ddMarker(ddP p, ddMarkerType type = ddMarkerType::Plus, int sizePx = 4, uint16_t color = TFT_WHITE, bool filled = false)
```

| Parameter | Description |
|---|---|
| `p` | Position (world units). |
| `type` | Marker shape (default Plus). |
| `sizePx` | Half-size in pixels (default 4). |
| `color` | RGB565 colour. |
| `filled` | Fill (Diamond, Square, Circle). |


## struct ddArc

```cpp
struct ddArc : ddVectorPrimitive
```

A circular arc, optionally closed through the centre as a pie slice.

**Fields**

- `Radius` world units. `StartAngle`, `EndAngle` radians; the sweep runs from start to end (end < start = clockwise).
- `Pie` joins the ends through the centre. `Filled` fills a Pie (concave pies are fine).

**Behaviour**

Drawn and collided as a polyline whose segment count follows the on-screen size.

**Pitfalls**

- `Filled` only has an effect when `Pie` is true.

**Tips and notes**

- Animate a gauge by changing EndAngle each frame.

---
### ddArc::ddArc()

```cpp
ddArc(ddP c, float radius, float startAngle, float endAngle, uint16_t color = TFT_WHITE, bool pie = false, bool filled = false)
```

| Parameter | Description |
|---|---|
| `c` | Centre (world units). |
| `radius` | World units. |
| `startAngle,endAngle` | Radians. |
| `color` | RGB565 colour. |
| `pie` | Close through the centre. |
| `filled` | Fill the pie. |


## struct ddBezier

```cpp
struct ddBezier : ddVectorPrimitive
```

A quadratic (3 control points) or cubic (4) Bezier curve.

**Fields**

- `P[4]` the control points (local coordinates), `Count` 3 or 4. Eval(t) gives the point at parameter t in 0..1.

**Behaviour**

Flattened to a polyline whose segment count follows the on-screen length of the control polygon (4..64). Bounds are the control points' box
(the curve is always inside it). Collides as a 24-segment polyline.

---
### ddBezier::ddBezier()

```cpp
ddBezier(ddP p0, ddP p1, ddP p2, uint16_t color = TFT_WHITE)
```

Quadratic Bezier.

| Parameter | Description |
|---|---|
| `p0,p2` | End points. |
| `p1` | Control point. |
| `color` | RGB565 colour. |


### ddBezier::ddBezier() (overload 2)

```cpp
ddBezier(ddP p0, ddP p1, ddP p2, ddP p3, uint16_t color = TFT_WHITE)
```

Cubic Bezier.

| Parameter | Description |
|---|---|
| `p0,p3` | End points. |
| `p1,p2` | Control points. |
| `color` | RGB565 colour. |


### ddBezier::Eval()

```cpp
ddP Eval(float t) const
```

Point on the curve.

| Parameter | Description |
|---|---|
| `t` | Parameter, 0 = first control point, 1 = last. Values outside 0..1 extrapolate. |

**Returns:** The point in local coordinates.


## struct ddSprite

```cpp
struct ddSprite : ddVectorPrimitive
```

An image (an LGFX_Sprite) drawn through the primitive's transform: rotation and uniform/axis scale are honoured, shear is not.

**Fields**

- `Src` the image (NOT owned: it must outlive the primitive). `UnitsPerPixel` world units per image pixel. `PivotX`, `PivotY` the pivot in image pixels (centre by default; the local origin).
- `HasTransparent` + `Transparent` an RGB565 colour key to skip.

**Behaviour**

Drawn with LovyanGFX pushRotateZoom (nearest-neighbour). Hit-tests and collides as its (rotated) rectangle.

**Pitfalls**

- The image is drawn nearest-neighbour; scaling up shows pixels and rotating can show jaggies.
- Mirroring (negative scale) is not supported by the underlying call.

---
### ddSprite::ddSprite()

```cpp
ddSprite(ddP p, LGFX_Sprite *src, float unitsPerPixel = 1.0f)
```

| Parameter | Description |
|---|---|
| `p` | Position of the pivot (world units). |
| `src` | The image. Must stay alive and unchanged in size. |
| `unitsPerPixel` | World units per image pixel (default 1). |


## struct ddText

```cpp
struct ddText : ddVectorPrimitive
```

A text label centred on its position, in LovyanGFX's built-in 6x8 Font0.

**Fields**

- `Text` up to 63 characters (copied). Change it with SetText().

**Behaviour**

Always pixel-sized (it does not scale with the world or the transform's scale; only the position is used). No bounds (never culled) and no collision geometry.

---
### ddText::ddText()

```cpp
ddText(ddP p, const char *text, uint16_t color = TFT_WHITE)
```

| Parameter | Description |
|---|---|
| `p` | Centre position (world units). |
| `text` | Text (truncated to 63 characters). |
| `color` | RGB565 colour. |


### ddText::SetText()

```cpp
void SetText(const char *text)
```

Replaces the text (truncated to 63 characters, always NUL-terminated).

**Pitfalls**

- Formatting numbers into the text with snprintf each frame costs time; only update when the value changes.


## struct ddGrid

```cpp
struct ddGrid : ddVectorPrimitive
```

A grid of lines with optional axes, in local space: lines every `Spacing` out to +/-`HalfW` and +/-`HalfH`.

**Fields**

- `Spacing`, `HalfW`, `HalfH` world units. `Color` the grid lines, `AxisColor` the two axes. `ShowAxes`.

**Behaviour**

Useful as a visual reference. At most 200 lines are drawn in each direction. Has bounds (so it is culled when off-screen) but no hit or collision geometry.

---
### ddGrid::ddGrid()

```cpp
ddGrid(float spacing, float halfW, float halfH, uint16_t color = 0x2104, uint16_t axisColor = TFT_DARKGREY)
```

| Parameter | Description |
|---|---|
| `spacing` | Distance between lines in world units (<= 0 draws nothing). |
| `halfW,halfH` | Half extents in world units. |
| `color` | Grid colour (default dark gray). |
| `axisColor` | Axis colour. |


## class ddGroup

```cpp
class ddGroup : public ddVectorPrimitive
```

A primitive that contains other primitives and gives them a shared transform (a node in a scene graph).

**Fields**

- `Children` a [RenderBlock](API-ddVector#class-renderblock) sharing the parent's arena. Add, remove, pick and iterate children through it.

**Behaviour**

Transforms compose down the tree (child transform inside the group's). Groups can nest. Bounds are the union of the visible children's bounds (unbounded if any child is).
Picking a child returns the TOP-LEVEL group. Removing a group destroys all its children.

```cpp
ddGroup *car = scene.EmplaceGroup();
car->Children.Emplace<ddRect>(ddP(0, 0), 20.0f, 8.0f);
car->Children.Emplace<ddCircle>(ddP(-7, -4), 3.0f);
car->SetPos(50, 0); car->SetAngle(0.3f);        // moves and rotates every child
```

**Pitfalls**

- Children have their own Visible flags, but a hidden group hides everything inside it.

**Tips and notes**

- The child pointers stay valid until you remove them or their group.

---
### ddGroup::ddGroup()

```cpp
explicit ddGroup(ddArena *a)
```

Creates a group whose children use the given arena. Use [RenderBlock](API-ddVector#class-renderblock)::EmplaceGroup() instead of calling this directly.


### ddGroup::~ddGroup()

```cpp
~ddGroup()
```

Destroys all children (a no-op during an arena reset, which already destroys everything).
