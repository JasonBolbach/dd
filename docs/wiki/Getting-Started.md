# Getting started

## 1. Install

dd is a PlatformIO / Arduino library that depends on LovyanGFX.

```ini
; platformio.ini
lib_deps =
    lovyan03/LovyanGFX@^1.2.21
    https://github.com/JasonBolbach/dd.git
```

Developing dd itself next to a project: put the checkout beside your project and use `lib_deps = symlink://../path/to/dd`, or add its parent folder to `lib_extra_dirs`.

```cpp
#include <LovyanGFX.hpp>
#include <dd.h>          // everything except the desktop-only ddSDLInput.h
```

## 2. A first scene

```cpp
#include <LovyanGFX.hpp>
#include <dd.h>

LGFX_Sprite *spr;                 // an off-screen buffer you push to the display each frame
ddR *r;                           // the renderer: maps world units to pixels
RenderBlock scene;                // the scene: owns an arena (default cap 256 KB)
ddCircle *ball;

void setup() {
  /* create your display (lcd), then the sprite: */
  spr = new LGFX_Sprite(&lcd);
  spr->setColorDepth(16);
  spr->createSprite(lcd.width(), lcd.height());

  r = new ddR(spr, -100, 100, -100, 100);        // show world x -100..100, y -100..100 (y is UP)

  scene.Emplace<ddGrid>(20.0f, 100.0f, 100.0f);   // a reference grid
  ball = scene.Emplace<ddCircle>(ddP(0, 0), 10.0f, TFT_CYAN, true);
  ddRect *box = scene.Emplace<ddRect>(ddP(50, 0), 20.0f, 20.0f, TFT_GREEN);
  box->SetAngle(0.5f);
}

void loop() {
  ball->SetPos(sinf(millis() * 0.002f) * 60.0f, 0);   // change any property, any time
  spr->clear();
  r->ResetStats();
  scene.Draw(r);                                       // transforms + culling + drawing
  spr->pushSprite(0, 0);
}
```

What to notice:

- **World units are yours.** The world rectangle you give `ddR` is fitted to the sprite with one uniform scale (no distortion); y points up.
- **`Emplace<T>(...)` creates an object in the arena and returns a pointer.** Keep it to change the object later. It returns `nullptr` if the arena is full (see [Performance and memory](Performance-and-Memory)).
- **Never `delete` an object.** Use `scene.Remove(p)`, `scene.Clear()`.
- **Constructors that take a position place the object there** and it rotates / scales about that point.

## 3. Add interaction

```cpp
ddInteract in(r, &scene);
in.OnEvent = [](ddEvent e, ddVectorPrimitive *p, const ddP &world, void *) {
  if (e == ddEvent::Tap && p) p->Color = TFT_RED;
};
ball->Draggable = true;

void loop() {
  int16_t x, y;
  bool down = lcd.getTouch(&x, &y);              // display pixels
  in.Update(down, ddP(x, y));                    // call EVERY frame
  /* draw ... */
  in.DrawSelection();
}
```

Or ask directly what is under a point: `scene.Pick(r->ToWorld(ddP(x, y)), 4.0f / r->PixelsPerUnit())`.

## 4. Ask geometric questions

```cpp
ddCircle *a = scene.Emplace<ddCircle>(ddP(0, 0), 10.0f);
ddRect   *b = scene.Emplace<ddRect>(ddP(12, 0), 8.0f, 8.0f);
bool touching = ddOverlap(a, b);
ddP hit1, hit2;
int  contacts = ddCollide(a, b, &hit1, &hit2);      // 0, 1, 2 ... boundary points
```

Pure geometry (no scene) lives in `ddGeom.h`: `ddCollideLineCircle`, `ddCollideLineLine`, `ddCollideCircleCircle`, `ddDistPointLine`, `ddBox` ... See the [Collision guide](Collision-Guide).

## 5. Run the desktop demo and tests

Both need SDL2 (`brew install sdl2 cmake` on macOS) and a LovyanGFX checkout next to the dd repository (or `-DLGFX_ROOT=...`).

```
cmake -S examples/DesktopDemo -B build-demo && cmake --build build-demo -j && build-demo/dd_demo
cmake -S extras/tests         -B build-test && cmake --build build-test -j && build-test/dd_tests
```

The demo is the best tour of what exists: drag the shapes, click the buttons, resize the window.

## 6. Build options

| Option | Effect |
|---|---|
| `DD_DEBUG` (define) | Enables the float formatting helpers (`ddP::ToString`, `ddStr`, `ddT::Print`, point coordinate labels). **Leave it off in production**: float `printf` is slow and large on microcontrollers. |
| `DD_ARENA_DEFAULT_MAX` | Default arena cap in bytes (256 KB). |
| `DD_ARENA_CHUNK` | Arena growth step (8 KB). |

## Running on a real board

- For a large sprite, call `spr->setPsram(true)` before `createSprite()` so the pixel buffer lives in PSRAM.
- The arena also tries PSRAM first, then falls back to normal heap.
- Use your own touch reading for `ddInteract`; `ddSDLInput.h` is desktop-only.
- This path has not been exercised on hardware yet: please report anything that behaves differently.
