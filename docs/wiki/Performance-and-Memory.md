# Performance and memory

## Measured numbers (desktop, to give a sense of proportion)

From the stress test in [`extras/tests`](https://github.com/JasonBolbach/dd/tree/main/extras/tests): 6,000 mixed primitives (circles, rectangles, polygons, lines, points, ellipses; about a sixth of them
on screen) drawn into a 1280x960 sprite, **release build on a desktop CPU**.

| | Result |
|---|---|
| Frame, culling **on** | about 1.0 ms |
| Frame, culling **off** | about 1.8 ms |
| 200 frames with 100 object swaps (Remove + Emplace) and animated transforms per frame | about 1.0 ms/frame, arena capacity unchanged (776 KB) |
| Memory for 6,000 objects | about 765 KB in use (about 127 bytes per object on a 64-bit host, matching the table below) |
| 20,000 random add/remove swaps of *different* kinds | capacity grew 776 KB to 784 KB |

A microcontroller is far slower than that, so treat these as **ratios and shapes of behaviour**, not promises: culling roughly halved the work here, recycling kept memory flat, and per-object cost is small.
Measure on your board with the HUD approach in the demo (frame time, `r->Drawn` / `r->Culled`, `scene.Arena()->InUse()`).

## Memory

### How much does an object cost?

Measured on a 64-bit host (`sizeof` plus the arena's 16-byte header; a 32-bit MCU is smaller because pointers are 4 bytes):

| Object | Bytes in the arena |
|---|---|
| `ddCircle`, `ddEllipse`, `ddRect`, `ddNGon`, `ddMarker`, `ddArc`, `ddGrid`, `ddPoint` | 120-128 |
| `ddLine`, `ddSprite` | 136 |
| `ddPolygon` (has a destructor to free its points) | 168, plus 12 bytes per point when created with `EmplacePolygon` |
| `ddArrow` | 152 |
| `ddBezier` | 168 |
| `ddText` (64-character buffer) | 176 |
| `ddGroup` (own block, plus its children) | 192 |

Of each object, 96 bytes is the common base: the transform (56), the pen (4), colour / flags / tag, the draw-list links and the vtable pointer. That is what makes every object independently movable, hideable and pickable.
An object with a non-trivial destructor (a group, an arena-owned polygon) carries a few more bytes of bookkeeping.

### Sizing the cap

1. Build your busiest scene.
2. Read `scene.Arena()->Peak()` (the most bytes ever live at once).
3. Set the cap a bit above it: `RenderBlock scene(peak * 5 / 4);` (or define `DD_ARENA_DEFAULT_MAX`).

The cap counts memory taken from the system, so it is the real RAM ceiling. `Emplace` returns `nullptr` when it would be exceeded, and `Arena()->Overflowed()` stays true so you can notice in testing.

### Recycling and its limit

`Remove()` puts a block on a free list for its size class (16-byte steps up to 1 KB; a block may also serve a request up to 128 bytes smaller). That keeps add/remove churn of **similar** objects flat.
If your scene's mix of *sizes* changes completely over time (many large objects freed, then only small ones allocated), freed blocks of the wrong size just sit there and `Capacity()` can creep up.
The remedy is simple: `Clear()` (which keeps the chunks and resets everything) at a quiet moment, or keep long-lived and short-lived objects in separate scenes.

### The sprite is the big allocation

A 16-bit sprite costs width x height x 2 bytes (320x240 = 150 KB; 480x320 = 300 KB). On an ESP32 call `sprite.setPsram(true)` before `createSprite()` for anything large. dd's arena is separate and small by comparison.

### Per-frame scenes cost nothing extra

An owning `RenderBlock::Clear()` resets the arena but keeps its chunks, so "Clear, then Emplace everything" each frame does **no** `malloc` or `free` after the first frame.

## Speed

### What to do

- **Keep culling on** (the default). Give things bounds: all primitives except `ddText` and `ddMarker` have them.
- **Hide instead of removing** things that come and go (`Visible = false`): zero cost while hidden.
- **Use retained mode** for big static scenes: modify only what changes. Setting a transform marks it dirty and the matrix is rebuilt once per frame, only if it was touched.
- **Group things that move together** (one transform for the lot). A group's bounds are the union of its children, so it is culled as one when none are visible.
- **Prefer the cheap shape**: `ddPoint`/`ddCircle` (LovyanGFX fast paths) over polygons; `ddNGon`/`ddRect` over `ddPolygon`.
- **Use `Pick` sparingly**: it scans the scene from the top. Call it on press, not every frame for hover unless the scene is small.

### What costs more

| Thing | Why | Mitigation |
|---|---|---|
| Wide pens (`Pen.Width > 1`) | `drawWideLine` is much slower than `drawLine` | Use 1 px for bulk geometry; wide only for emphasis |
| Dashed / dotted pens | many tiny segments | Use sparingly; dashed circles/rects fall to the polyline path |
| Non-plain pen on a circle or rect | disables the LovyanGFX fast path (polyline instead) | Keep bulk circles/rects plain |
| Large filled concave polygons | scanline fill over its height | Fill is O(height x edges); simplify the outline |
| Rotated ellipses and non-uniform-scaled circles | polyline path | Fine for a few; avoid thousands |
| `ddMarker` / `ddText` off-screen | no bounds, never culled | Hide or remove the ones you do not need |
| Polygons with huge vertex counts | bounds and drawing are O(n) every frame | Simplify, or split into culled pieces |
| `DrawPoint` coordinate labels | float `printf` every frame (DD_DEBUG only) | Leave `DD_DEBUG` off in production |
| `ddFindOverlaps` / `Pick` over big scenes | linear scan (bounding-box rejection first) | Query only objects that moved; keep probes small |
| Collision of big polygons | O(edges_a x edges_b) with per-edge box rejection | Test bounding boxes first (`ddBoxesTouch`), simplify shapes |

### Float-only on purpose

dd uses `float` everywhere (and the demo is compiled with `-Wdouble-promotion` to catch accidents). On parts with a single-precision FPU only, `double` falls back to slow software maths.
Float formatting (`printf("%f")`) is the one place a `double` appears (C varargs promote it); it is compiled out unless `DD_DEBUG` is defined.

## Build flags that matter

- Release / `-O2` (or `-Os` on MCU) for any timing. Debug builds can be 3-5x slower.
- Keep `DD_DEBUG` **off** for production.
- If flash is tight, the only large dependency is LovyanGFX itself; dd is header-only and unused primitives are not linked.
