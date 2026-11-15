# ddInteract.h

*Touch / mouse interaction.*  Source: [`src/ddInteract.h`](https://github.com/JasonBolbach/dd/blob/main/src/ddInteract.h)

- A PRESS selects the topmost primitive under the pointer (with a pixel-sized tolerance).
- DRAGGING moves primitives marked `Draggable` (top-level primitives of the scene).
- A TAP is a quick press-and-release without moving.

```cpp
ddInteract in(&renderer, &scene);
in.OnEvent = [](ddEvent e, ddVectorPrimitive *p, const ddP &world, void *) {
  if (e == ddEvent::Tap && p) onTapped(p);
};
shape->Draggable = true;

void loop() {
  ddP t; bool down = readTouch(&t);        // display pixels
  in.Update(down, t);                      // once per frame
  scene.Draw(&renderer);
  in.DrawSelection();
}
```

**Pitfalls**

- Feed Update() EVERY frame, including frames where nothing is touched, or releases and taps are never seen.

**Tips and notes**

- On the desktop, a click shorter than one frame is still seen when you read the mouse through [ddSDLInput](API-ddSDLInput#class-ddsdlinput) (it latches presses).

## Contents

- [enum ddEvent](#enum-ddevent)
- [ddEventFn](#ddeventfn)
- [ddNowMs()](#ddnowms)
- [class ddInteract](#class-ddinteract)

## enum ddEvent

```cpp
enum struct ddEvent
```

What happened to a primitive: Press (pointer went down on it), Drag (moved while down, after passing the tap distance),
Release (pointer went up), Tap (a quick Release without movement; delivered after the Release).

---
## ddEventFn

```cpp
typedef void (*ddEventFn)(ddEvent ev, ddVectorPrimitive *p, const ddP &world, void *user)
```

Event callback signature.

| Parameter | Description |
|---|---|
| `ev` | The event. |
| `p` | The primitive the pointer pressed (never null for the events generated here). |
| `world` | Pointer position in world units. |
| `user` | The `User` pointer you set on the [ddInteract](API-ddInteract#class-ddinteract). |

**Tips and notes**

- A plain function pointer (no std::function), so it costs no heap on microcontrollers. A capture-less lambda converts to it.

---
## ddNowMs()

```cpp
inline uint32_t ddNowMs()
```

Millisecond clock: Arduino millis() on the boards, steady_clock on the desktop. Wraps after ~49 days; differences are what matter.

---
## class ddInteract

```cpp
class ddInteract
```

Pointer interaction state machine: selection, dragging and taps over a scene.

**Fields**

- `OnEvent`, `User` callback and its user pointer. `PickTolPx` pick slop in pixels (default 4). `TapMaxPx` / `TapMaxMs` limits for a tap (6 px, 400 ms).
- `Selected` the last pressed primitive (cleared by a tap on empty space). `Active` the primitive under the pointer right now.

**Pitfalls**

- `Selected` and `Active` are raw pointers. Call Forget(p) before removing a primitive so they do not dangle.
- Only TOP-LEVEL primitives of the scene are picked (a child of a group is picked as the group).

---
### ddInteract::ddInteract()

```cpp
ddInteract(ddR *r, RenderBlock *scene)
```

| Parameter | Description |
|---|---|
| `r` | The renderer (used to convert display pixels to world units). |
| `scene` | The scene to pick from. |


### ddInteract::Update()

```cpp
void Update(bool down, const ddP &display)
```

Advances the interaction. Call once per frame.

| Parameter | Description |
|---|---|
| `down` | true while the pointer / finger is down. |
| `display` | Pointer position in DISPLAY pixels (what a touch controller reports). |

**Pitfalls**

- Dragging only moves primitives whose `Draggable` is true; Drag events fire either way.

**Tips and notes**

- Events fire from inside this call (OnEvent), in this order on a press: Press; while held and moved: Drag; on release: Release then, for a tap, Tap.


### ddInteract::Forget()

```cpp
void Forget(const ddVectorPrimitive *p)
```

Clears `Selected` / `Active` if they point at p. Call it before [RenderBlock](API-ddVector#class-renderblock)::Remove(p).


### ddInteract::DrawSelection()

```cpp
void DrawSelection(uint16_t color = TFT_WHITE)
```

Draws a dashed rectangle around the selected primitive (if it has known bounds). Call after drawing the scene.

| Parameter | Description |
|---|---|
| `color` | RGB565 outline colour (default white). |

**Tips and notes**

- The box is the primitive's axis-aligned world bounds, so a rotated shape gets a larger box than the shape.
