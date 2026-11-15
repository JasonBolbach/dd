# ddRender.h

*Renderer, pens, polygon fill.*  Source: [`src/ddRender.h`](https://github.com/JasonBolbach/dd/blob/main/src/ddRender.h)

[ddR](API-ddRender#class-ddr) sits between your world coordinates (y up, any units you like) and a LovyanGFX sprite (pixels, y down). Scene
primitives (ddVector.h) draw THROUGH a [ddR](API-ddRender#class-ddr); you can also call its Draw* helpers directly for immediate-mode drawing.

**Tips and notes**

- The only dependency is LovyanGFX. The display or sprite is yours: dd never creates one.
- Everything is float. Pixel coordinates are rounded (lroundf), not truncated, so animated shapes do not jitter.

## Contents

- [delayMs()](#delayms)
- [enum ddLineStyle](#enum-ddlinestyle)
- [struct ddPen](#struct-ddpen)
- [class ddR](#class-ddr)

## delayMs()

```cpp
inline void delayMs(unsigned int ms)
```

Portable millisecond delay: Arduino delay() on the boards, std::this_thread::sleep_for on the desktop.

| Parameter | Description |
|---|---|
| `ms` | Milliseconds to wait. |

**Tips and notes**

- Defined twice (once per branch of an #if); the Arduino one is shown.

---
## enum ddLineStyle

```cpp
enum struct ddLineStyle : uint8_t
```

Dash pattern of a [ddPen](API-ddRender#struct-ddpen): Solid, Dashed (pen.Dash / pen.Gap pixels) or Dotted (round dots sized from the pen width).

---
## struct ddPen

```cpp
struct ddPen
```

Stroke settings for line-based shapes. All sizes are in PIXELS (they do not scale with the world).

```cpp
ddLine *l = scene.Emplace<ddLine>(ddP(-50, 0), ddP(50, 0));
l->Pen.Width = 3;
l->Pen.Style = ddLineStyle::Dashed;
l->Pen.Dash = 8; l->Pen.Gap = 4;
```

**Pitfalls**

- Wide pens use LovyanGFX's drawWideLine, which is noticeably slower than a 1-pixel line.
- A non-plain pen on a circle or rectangle disables the fast circle / rect paths and draws the shape as a polyline instead (still correct, a little slower).

**Tips and notes**

- Dotted with width 1 draws single pixels.

---
### ddPen::Plain()

```cpp
bool Plain() const
```

**Returns:** true for a 1-pixel solid pen. Shapes use this to decide whether LovyanGFX's fast primitives can be used.


## class ddR

```cpp
class ddR
```

Renderer: maps a world rectangle onto a sprite and draws world-space primitives.

The world rectangle is fitted into the sprite with ONE uniform scale (no distortion): if the aspect ratios differ the
world is centred with empty bars. World y points UP, display y points DOWN; [ddR](API-ddRender#class-ddr) flips it.

```cpp
LGFX_Sprite spr(&lcd); spr.createSprite(320, 240);
ddR r(&spr, -160, 160, -120, 120);          // 1 world unit = 1 pixel here, origin in the middle
ddP onScreen = r.ToDisplay(ddP(0, 0));      // (160, 120)
ddP inWorld  = r.ToWorld(ddP(160, 120));    // (0, 0)  - e.g. a touch point
```

**Pitfalls**

- Holds a raw pointer to the sprite. The sprite must outlive the [ddR](API-ddRender#class-ddr), and if it is recreated at another size you must call Refresh().

**Tips and notes**

- Draw* functions take a `World` flag: true (default) = the point is in world units, false = already in display pixels. Mixing them up is the most common mistake: a shape at "(160,120)" in world units is far off-screen.

---
### Sprite

```cpp
LGFX_Sprite *Sprite
```

The target sprite (public so primitives can call LovyanGFX directly).


### WorldYMax

```cpp
float WorldXMin, WorldXMax, WorldYMin, WorldYMax
```

The world rectangle that Refresh() fits to the sprite. Change them, then call Refresh().


### D2W

```cpp
Matrix3x3 W2D, D2W
```

W2D: world to display pixels. D2W: its inverse (display pixels to world, e.g. for touch). Valid after Refresh().


### Tmp

```cpp
std::vector<ddP> Tmp
```

Scratch vertex buffer shared by primitives while drawing (display-space points). Grows once, then is reused, so
drawing does no per-frame allocation. Do not hold references across a draw call.


### Xs

```cpp
std::vector<float> Xs
```

Scratch buffer used by the polygon scanline fill.


### ViewXMin

```cpp
float ViewXMin = 0, ViewXMax = 0, ViewYMin = 0, ViewYMax = 0
```

The WORLD rectangle actually covered by the sprite (after letterboxing). This is what culling tests against.
Valid after Refresh().


### CullEnabled

```cpp
bool CullEnabled = true
```

Master switch for culling. true (default): primitives with known bounds that lie outside the view are not drawn.
Turn it off to measure what culling saves, or if you draw outside the sprite on purpose.


### Drawn

```cpp
uint32_t Drawn = 0, Culled = 0
```

Per-frame statistics: primitives drawn / skipped by culling. Groups count themselves and their children separately.
Call ResetStats() at the start of each frame.


### ddR::ResetStats()

```cpp
void ResetStats()
```

Zeroes Drawn and Culled. Call once at the start of every frame.


### ddR::ddR()

```cpp
ddR(LGFX_Sprite *Spr, float WXMin, float WXMax, float WYMin, float WYMax)
```

Creates the renderer and computes the mappings (Refresh()).

| Parameter | Description |
|---|---|
| `Spr` | The sprite (or any LGFX_Sprite-compatible target) to draw into. |
| `WXMin` | World x shown at the left edge (before letterboxing). |
| `WXMax` | World x at the right edge. |
| `WYMin` | World y at the BOTTOM edge. |
| `WYMax` | World y at the top edge. |

**Pitfalls**

- The sprite must already be created (non-zero size) when you construct the [ddR](API-ddRender#class-ddr).


### ddR::ToDisplay()

```cpp
ddP ToDisplay(const ddP &p) const
```

Converts a world point to display pixels.

**Returns:** Pixel position (float; round it for drawing). Y is flipped.


### ddR::ToWorld()

```cpp
ddP ToWorld(const ddP &p) const
```

Converts display pixels to a world point (for touch and mouse input).

**Returns:** World position.


### ddR::PixelsPerUnit()

```cpp
float PixelsPerUnit() const
```

**Returns:** The uniform scale: display pixels per world unit. Use it to convert world sizes to pixel sizes.


### ddR::InView()

```cpp
bool InView(float x0, float y0, float x1, float y1, float padPx = 3.0f) const
```

Culling test: does a world-space box overlap the visible area?

| Parameter | Description |
|---|---|
| `x0,y0,x1,y1` | The box (min/max corners, world units). |
| `padPx` | Safety margin in pixels (default 3) so thick strokes at the edge are not culled. |

**Returns:** true if the box overlaps the view (or touches the margin).


### ddR::Refresh()

```cpp
void Refresh()
```

Recomputes W2D, D2W and the view rectangle from the world bounds and the sprite's CURRENT size.

**Pitfalls**

- Divides by the world width/height: a degenerate (zero-size) world rectangle gives infinities.

**Tips and notes**

- Called by the constructor. Call it again after changing WorldXMin..WorldYMax or recreating the sprite.


### ddR::LinePx()

```cpp
void LinePx(float x0, float y0, float x1, float y1, uint16_t color, int width = 1)
```

Draws one solid segment in DISPLAY pixels.

| Parameter | Description |
|---|---|
| `width` | Pixels; 1 uses the fast drawLine, more uses drawWideLine. |


### ddR::StrokeSegment()

```cpp
void StrokeSegment(float x0, float y0, float x1, float y1, uint16_t color, const ddPen &pen, float &phase)
```

Draws one pen-styled segment in DISPLAY pixels, honouring dashes and dots.

| Parameter | Description |
|---|---|
| `phase` | In/out: position within the dash pattern. Pass the same variable to consecutive segments of a polyline so               the pattern flows around corners; start it at 0. |

**Tips and notes**

- This is the primitive every dashed shape is built on.


### ddR::DrawLine()

```cpp
void DrawLine(ddP p1, ddP p2, uint16_t color, bool World = true, const ddPen &pen = ddPen())
```

Immediate-mode line.

| Parameter | Description |
|---|---|
| `p1,p2` | End points. |
| `color` | RGB565 colour (TFT_* constants or lcd.color565()). |
| `World` | true: points are world units; false: display pixels. |
| `pen` | Width and dash style (default: 1-pixel solid). |


### ddR::DrawString()

```cpp
void DrawString(ddP p, const char *str, uint16_t color, bool World = true)
```

Draws text with its top-left corner at p (built-in 6x8 Font0).

| Parameter | Description |
|---|---|
| `str` | NUL-terminated text. |

**Tips and notes**

- Text is not scaled by the world; it is always pixel-sized.


### ddR::DrawCenterString()

```cpp
void DrawCenterString(ddP p, const char *str, uint16_t color, bool World = true)
```

Draws text centred on p (built-in Font0). Same parameters as DrawString.


### ddR::DrawCircle()

```cpp
void DrawCircle(ddP c, int radius, uint16_t color, bool World = true)
```

Circle outline.

| Parameter | Description |
|---|---|
| `c` | Centre (world units, or pixels if World is false). |
| `radius` | In PIXELS (not world units). Multiply a world radius by PixelsPerUnit() first. |


### ddR::DrawEllipse()

```cpp
void DrawEllipse(ddP c, int rx, int ry, uint16_t color, bool World = true)
```

Axis-aligned ellipse outline.

| Parameter | Description |
|---|---|
| `rx,ry` | Radii in PIXELS. For rotated ellipses use the [ddEllipse](API-ddVector#struct-ddellipse) primitive. |


### ddR::FillCircle()

```cpp
void FillCircle(ddP c, int radius, uint16_t color, bool World = true)
```

Filled disc.

| Parameter | Description |
|---|---|
| `radius` | In PIXELS. |


### ddR::FillEllipse()

```cpp
void FillEllipse(ddP c, int rx, int ry, uint16_t color, bool World = true)
```

Filled axis-aligned ellipse.

| Parameter | Description |
|---|---|
| `rx,ry` | Radii in PIXELS. |


### ddR::FillPolygonScreen()

```cpp
void FillPolygonScreen(const ddP *p, int n, uint16_t color)
```

Fills a polygon given in DISPLAY pixels using an even-odd scanline fill (convex or concave, self-intersections leave holes).

| Parameter | Description |
|---|---|
| `p` | Vertices. |
| `n` | Vertex count (< 3 draws nothing; exactly 3 uses LovyanGFX's triangle fill). |
| `color` | RGB565 fill colour. |

**Tips and notes**

- Uses the Xs scratch buffer: no allocation after the first call.


### ddR::DrawPolyScreen()

```cpp
void DrawPolyScreen(const ddP *p, int n, bool closed, bool filled, uint16_t color, const ddPen &pen = ddPen())
```

Draws a polyline or polygon given in DISPLAY pixels.

| Parameter | Description |
|---|---|
| `p` | Vertices. |
| `n` | Vertex count. |
| `closed` | true: also connect the last vertex back to the first. |
| `filled` | true: fill instead (implies closed; the pen is ignored). |
| `pen` | Stroke for outlines. |

**Tips and notes**

- The dash pattern continues across vertices and around the closing edge.


### ddR::DrawPoint()

```cpp
void DrawPoint(ddP p, int radius, uint16_t color, bool World = true, bool Label = true)
```

Filled dot, optionally labelled with its world coordinates.

| Parameter | Description |
|---|---|
| `radius` | In PIXELS. |
| `Label` | Draws "(x, y)" above the dot. Only honoured when DD_DEBUG is defined (it needs float printf). |

**Pitfalls**

- Labels allocate a stack buffer and format floats every frame: leave them off in production.
