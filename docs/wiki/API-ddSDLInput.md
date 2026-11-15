# ddSDLInput.h

*Desktop mouse mapping.*  Source: [`src/ddSDLInput.h`](https://github.com/JasonBolbach/dd/blob/main/src/ddSDLInput.h)

**Why**

The SDL panel does not stretch its frame to the window: it scales it in 0.5 steps (floor(2*out/frame)/2) and CENTRES it, so a larger window shows bars
around the image, and on hi-dpi displays the renderer output is bigger than the window in points. `lcd.getTouch()` maps the mouse across the WHOLE
window, so it drifts as soon as bars appear. Here the mouse is mapped with exactly the panel's maths, and presses that land in the bars are ignored.

**Aspect lock (optional)**

When the window is resized it is snapped back to the frame's aspect ratio, so the panel's independent X/Y scale steps stay equal and the image is never stretched.

```cpp
void setup() { lcd.init(); ddSDLInput::Begin(LCD_W, LCD_H); }
void loop()  { float x, y; bool down = ddSDLInput::Get(x, y); }   // left button, logical pixels
```

**Pitfalls**

- Use it under `#ifdef LGFX_SDL`; on real hardware read the touch controller instead.
- Rotation of the panel (the L/R keys) is not accounted for.

**Tips and notes**

- The mapping is ported from SuperGUI's Form Designer (DesignerInput.h).

## Contents

- [class ddSDLInput](#class-ddsdlinput)

## class ddSDLInput

```cpp
class ddSDLInput
```

Static helper: install once, then poll the mouse. All state is static (one window).

---
### ddSDLInput::Begin()

```cpp
static void Begin(int logicalW, int logicalH, bool lockAspect = true)
```

Installs the SDL event watch. Call once after the display is created.

| Parameter | Description |
|---|---|
| `logicalW,logicalH` | The panel's frame size in pixels (e.g. the numbers you gave `LGFX(w, h)`). |
| `lockAspect` | true (default): keep the window at the frame's aspect ratio when it is resized. |

**Pitfalls**

- Call it with the SAME size as the panel, or clicks will land in the wrong place.


### ddSDLInput::Get()

```cpp
static bool Get(float &x, float &y)
```

Left-button state and position in LOGICAL pixels (the panel's pixel grid).

| Parameter | Description |
|---|---|
| `x,y` | Outputs: the last pointer position. After a drag leaves the image it can be negative or >= the logical size. |

**Returns:** true while the left button is down. A click shorter than one frame is LATCHED and reported as down once, so quick taps are not lost.

**Tips and notes**

- A press that starts in the bars (outside the image) is ignored.


### ddSDLInput::Scale()

```cpp
static void Scale(int wx, int wy, int ww, int wh, int rw, int rh, int lw, int lh, int &lx, int &ly)
```

Pure mapping from a window point to a logical pixel (no SDL calls; unit-tested).

| Parameter | Description |
|---|---|
| `wx,wy` | Mouse position in window points. |
| `ww,wh` | Window size in points. |
| `rw,rh` | Renderer output size in pixels (larger than the window on hi-dpi displays). |
| `lw,lh` | Logical frame size. |
| `lx,ly` | Outputs: logical pixel. Outside 0..lw-1 / 0..lh-1 means the pointer is in a bar. |

**Tips and notes**

- Uses integer division on purpose: it must match the panel's 0.5-step scale exactly.


### ddSDLInput::AspectFit()

```cpp
static void AspectFit(int w, int h, int prevW, int prevH, int lw, int lh, int &nw, int &nh)
```

Pure helper for the aspect lock: the window size that has the frame's aspect.

| Parameter | Description |
|---|---|
| `w,h` | The new window size. |
| `prevW,prevH` | The previous size. Whichever dimension moved most drives the other. |
| `lw,lh` | Logical frame size. |
| `nw,nh` | Outputs: the size to snap to. |
