# Concepts

How the pieces of dd fit together, and the rules that explain most of its behaviour.

## 1. Coordinate systems

There are two:

| Space | Units | Y axis | Origin |
|---|---|---|---|
| **World** | Whatever you choose (millimetres, metres, "game units") | **up** | Wherever your world rectangle puts it |
| **Display** | Pixels of your sprite | **down** | Top-left corner |

[`ddR`](API-ddRender#class-ddr) converts between them. You give it a world rectangle, e.g. `ddR r(&sprite, -160, 160, -120, 120)`, and it fits that rectangle into the sprite with **one uniform scale**
(no stretching). If the aspect ratios differ the world is centred and there are empty bars. `r.ToDisplay(p)` and `r.ToWorld(p)` convert points; `r.PixelsPerUnit()` converts sizes.

Almost everything in dd takes **world** units. The exceptions are things that are inherently pixel-sized: pen widths and dash lengths ([`ddPen`](API-ddRender#struct-ddpen)), [`ddMarker`](API-ddVector#struct-ddmarker) size, text, and the `Draw*` functions that say "radius in PIXELS".
The `World` flag on `ddR::Draw*` says which space a point is in: `true` = world (default), `false` = already display pixels.

**Touch and mouse input arrive in display pixels.** Convert with `r->ToWorld(ddP(x, y))` before testing against the scene.

## 2. Points, matrices and transforms

A point is [`ddP`](API-ddPoint#struct-ddp) (x, y, weight 1). Transforms are 3x3 matrices ([`Matrix3x3`](API-ddPoint#struct-matrix3x3)) so translation, rotation and scale compose by multiplication.
`a * b` applies **b first**.

Most of the time you use [`ddT`](API-ddPoint#struct-ddt): position + angle + scale, combined as **scale, then rotate, then translate** about the object's own origin.
Angles are **radians**, counter-clockwise (y up).

Every scene primitive owns one. The setters are **absolute** (`SetAngle(1.0f)` means "be at 1 radian", not "turn by 1 radian") and **lazy** (the matrix is rebuilt once when needed).

### Local origin and pivot

Each primitive's geometry is defined around its **local origin**; the transform places that origin in the world. Constructors that take a centre or position (`ddCircle(c, r)`, `ddRect(c, w, h)` ...)
put the object there by setting its position, so `SetAngle` spins it about its own centre. (`ddLine`, `ddArrow`, `ddBezier` and `ddPolygon` take points in local space instead, so they spin about the origin
of whatever space they are in until you give them a position.)

### Parent transforms and groups

A [`ddGroup`](API-ddVector#class-ddgroup) holds children and applies its own transform to all of them: a child's world matrix is `groupMatrix * childMatrix`. Groups nest. Move or rotate the group and everything inside follows.

## 3. The scene

A scene is a [`RenderBlock`](API-ddVector#class-renderblock): an ordered list of primitives (last added = drawn on top). You create objects with `Emplace<T>(...)` and get a pointer back.

- **Ownership:** the scene owns its objects. They live in an arena ([section 6](#6-memory)). **Never `delete` them.** Use `Remove(p)` for one, `Clear()` for all.
- **Pointer lifetime:** a pointer is valid until you `Remove` that object or `Clear` the scene. Anything else holding the pointer (your own list, a selection) must be told first.
- **Two styles:**
  - *Retained* - build once, then modify objects each frame. Cheapest.
  - *Per-frame* - `Clear()` and re-`Emplace` every frame. Fine too: an owning block keeps its memory, so there is no allocation.
- **Hiding:** `p->Visible = false` removes an object from drawing, picking and collision without destroying it. Cheaper than removing and re-adding.
- **Z-order:** `MoveToFront` / `MoveToBack`.
- **Your own data:** every primitive has a `Tag` (a `uint16_t`) the library never touches.

### Adding your own primitive

Derive from [`ddVectorPrimitive`](API-ddVector#struct-ddvectorprimitive), implement `DrawShape` (and optionally `HitLocal`, `LocalBounds`, `Shapes`), and `Emplace<YourType>(...)`. Derive **singly** (no multiple inheritance).

## 4. The drawing pipeline

`scene.Draw(r)` walks the list and, for each visible primitive:

1. combines its transform with the parent's matrix;
2. **culls** it if it has known bounds that are outside the view (`r->CullEnabled`, counters in `r->Drawn` / `r->Culled`);
3. calls its `DrawShape`, which draws through the renderer.

Shapes pick the cheapest correct way to draw. A circle uses LovyanGFX's fast circle when the transform keeps it round and the pen is plain; otherwise it becomes a polyline whose
segment count follows its on-screen size. A rectangle uses `drawRect` when axis-aligned. Filled polygons use a scanline fill that handles concave outlines.
Shapes with unknown bounds (`ddText`, `ddMarker`) are never culled.

## 5. Picking and interaction

`scene.Pick(worldPoint, tolerance)` returns the topmost visible primitive under a point. Filled shapes hit anywhere inside; unfilled shapes only near their outline; a group hits if any child does.

[`ddInteract`](API-ddInteract) turns a stream of pointer states into events (Press / Drag / Release / Tap) and drags objects marked `Draggable`. Feed it **every frame**.

## 6. Memory

Everything in a scene lives in a [`ddArena`](API-ddArena#class-ddarena):

- memory is obtained in **chunks** (8 KB), PSRAM first on ESP32, and never moves;
- the total is **capped** (default 256 KB, per scene). When the cap is hit, `Emplace` returns `nullptr` instead of crashing;
- `Clear()` resets everything and keeps the chunks;
- `Remove()` recycles the object's block onto a free list for its size class, so add/remove churn does not grow memory;
- `Arena()->InUse()`, `Peak()`, `Capacity()` and `Overflowed()` tell you what is happening.

Each object costs its own size plus a 16-byte header (and a few more bytes if it needs a destructor): about 120-180 bytes for most primitives on a 64-bit host (measured table in the performance page). See [Performance and memory](Performance-and-Memory).

## 7. Collision

Collision is separate from drawing. The maths ([ddGeom.h](API-ddGeom)) works on points, lines, circles and boxes; [ddCollide.h](API-ddCollide) applies it to scene primitives
by turning each into **parts** (points, circles, segments, polygons) in world space. The rules are in the [Collision guide](Collision-Guide); the three to remember:

1. **Contacts are boundary points**, returned as a *count* plus optional output pointers (nothing is touched when the count is 0).
2. **Unfilled shapes are outlines.** A small unfilled circle inside a big one does not touch it. Filled shapes are solid. Pass `solid = true` to treat every closed shape as filled.
3. **A line has an extent:** `Segment` (default), `Ray`, or infinite `Line`.

## 8. Numerics

- **Floats only.** The library never uses `double`. Tolerances are relative to the *local* size of the shapes involved, so results do not degrade far from the origin.
- Float has about 7 significant digits. Shapes that only just touch can report either 0 or 1 contact. Do not build logic that needs exact tangency.
- Degenerate input is handled rather than rejected: zero-length segments act as points, zero-radius circles as points, negative radii never collide.

## 9. Threads and platforms

- Nothing in dd is thread-safe. Use one scene per thread or add your own locking. The collision functions share static scratch buffers (see [`ddCollide.h`](API-ddCollide)).
- Desktop (SDL): [`ddSDLInput.h`](API-ddSDLInput) maps the mouse correctly when the window is larger than the picture and can lock the window's aspect ratio.
- Microcontrollers: the arena uses PSRAM when present; `delayMs` and the interaction clock use Arduino `delay()` / `millis()`.

## 10. What dd does not do (yet)

- 3D (planned, not started).
- Text beyond LovyanGFX's built-in 6x8 font (no scaling, one line).
- Anti-aliasing, gradients, textured fills.
- Rotated ellipse collision is approximate (a 48-gon); arcs and Beziers collide as polylines.
- Spatial indexes: `Pick` and `ddFindOverlaps` scan the scene (with cheap bounding-box rejection).
