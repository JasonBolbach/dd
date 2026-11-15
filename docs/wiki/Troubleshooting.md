# Troubleshooting

## Nothing appears

| Check | Why |
|---|---|
| Did you call `spr->pushSprite(0, 0)` (or your display equivalent) after `scene.Draw(r)`? | dd draws into the sprite only. |
| Is the object inside the world rectangle you gave `ddR`? | Anything outside is culled. Try `r->CullEnabled = false` to test. |
| Is **y up**? | World y points up; a shape at y = +50 is in the upper half. |
| Did you use the right `World` flag? | `r->DrawLine(p1, p2, color)` expects world units; pass `false` for display pixels. A shape "at (160, 120)" in world units is off-screen if the world is -100..100. |
| Is `Visible` true? Is the colour black on black? | A hidden object draws nothing. |
| Did `Emplace` return `nullptr`? | The arena cap was reached: check `scene.Arena()->Overflowed()`, raise the cap. |
| Is the sprite created *before* you built `ddR`? | `ddR` reads the sprite size at construction. Call `r->Refresh()` after resizing. |
| Are you drawing a hair-thin shape? | A `ddPoint` with a tiny radius can round to 0 pixels. Use a larger radius or a `ddMarker`. |

## The shape is in the wrong place or spins about the wrong point

- Constructors with a *position* (`ddCircle(c, r)`, `ddRect(c, w, h)`, `ddNGon`, `ddArc`, `ddEllipse`, `ddPoint`, `ddText`, `ddMarker`, `ddSprite`) place the object, and it rotates about that point.
- `ddLine`, `ddArrow`, `ddBezier` and `ddPolygon` take points in **local** space. Give them a transform (`SetPos`) or build the points around (0, 0).
- `SetAngle` / `SetScale` are **absolute**, not incremental.
- Angles are **radians**, counter-clockwise. Use `DDD_DEG2RAD`.

## Crash or garbage after removing things

- After `scene.Remove(p)`, `scene.Clear()` or `Reset()`, every pointer to those objects is **dangling**. Drop your own copies (and call `ddInteract::Forget(p)` first).
- **Never `delete` a primitive.** Only the scene frees them.
- A `ddPolygon` built with the plain constructor does **not** copy its points; the array must outlive it. Use `scene.EmplacePolygon(...)` to have them copied.
- A `ddSprite` does not own its image; keep the `LGFX_Sprite` alive.
- Not thread-safe: do not touch a scene from two threads.

## Touch / mouse lands in the wrong place

- Convert pixels to world with `r->ToWorld(...)` before picking or colliding.
- **Desktop SDL:** `lcd.getTouch()` stretches the mouse across the whole window, so it drifts once the window is bigger than the picture. Use [`ddSDLInput`](API-ddSDLInput) (`Begin` once, then `Get(x, y)`); it maps with the panel's own scaling and ignores clicks in the bars.
- Quick taps on the desktop being missed: `ddSDLInput::Get` latches presses; if you read the mouse another way, a click shorter than one frame is invisible to per-frame polling.
- `ddInteract::Update` must be called **every frame**, including when nothing is touched, or releases and taps are never seen.

## Collision says "no" when shapes plainly overlap

- Unfilled shapes are **outlines**. A small unfilled circle inside a big one does not touch it. Use `Filled`, or pass `solid = true`.
- `ddCollide` / `ddContacts` count **boundary** contacts only. A shape wholly inside another has none: use `ddOverlap`.
- `ddCollideLineCircle` treats the circle as its rim: a segment fully inside returns 0. See [Collision guide](Collision-Guide#pitfalls).
- Hidden primitives and `ddText` / `ddGrid` have no collision geometry.
- A group's children collide with the group's transform applied; make sure you test the group, or the child with the right parent matrix.

## Things look jagged or wrong

- Sprites are drawn nearest-neighbour; scaling up shows pixels, rotating shows jaggies. There is no anti-aliasing.
- Dashed lines restarting oddly at corners: the pattern flows across vertices of one polyline or polygon; separate `ddLine`s each restart.
- A thick (wide-pen) circle or rectangle is drawn as a polyline: corners look polygonal at low segment counts. Set `Steps` on circles for a fixed segment count.
- Text is always the 6x8 built-in font and does not scale with the world.

## Memory or speed problems

See [Performance and memory](Performance-and-Memory). Quick checks: culling on, `DD_DEBUG` off, a release build, `Arena()->Peak()` to size the cap.

## Compile and build problems

| Message / symptom | Fix |
|---|---|
| `LovyanGFX.hpp` not found | Add `lovyan03/LovyanGFX` to `lib_deps`; for the desktop CMake builds put a LovyanGFX checkout next to the dd repository or pass `-DLGFX_ROOT=...`. |
| Desktop program says it cannot find libSDL3 | On current Homebrew `sdl2` is the sdl2-compat layer, which runs SDL2 programs on top of SDL3. `brew install sdl3` as well. dd itself only uses the SDL2 API. |
| `SDL.h` not found (desktop) | Install SDL2 (`brew install sdl2`). `ddSDLInput.h` is desktop-only; guard it with `#ifdef LGFX_SDL`. |
| `LGFX` is not a type | The library does not define your display class. In the demo, `LGFX_AUTODETECT.hpp` provides one for SDL. On hardware, define your own `LGFX` class. |
| Warnings about float to double promotion | In your own code use `sinf`/`cosf`, `1.0f` literals. The demo is built with `-Wdouble-promotion` to catch these. |
| `ToString` / `ddStr` / `Print` not found | They exist only with `DD_DEBUG` defined. |
| Over-aligned type error from the arena | Types with `alignof > 16` are rejected by design. |

## Still stuck?

The [desktop demo](https://github.com/JasonBolbach/dd/tree/main/examples/DesktopDemo) exercises almost everything and is the quickest place to see a working pattern. The test suite in [`extras/tests`](https://github.com/JasonBolbach/dd/tree/main/extras/tests) shows exact expected behaviour for edge cases.
