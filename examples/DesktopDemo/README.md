# dd desktop demo (SDL)

A showcase and stress test for dd in a 1280x960 SDL window (a 4:3 world of 320x240 units).

- **Top:** one of each primitive (drag them), pen styles (width, dashed, dotted), nested groups (solar system, clock), a filled concave gear, a gauge.
- **Below:** a field of moving particles (1500 to start) plus 1000 far-away objects that exist only to be culled.
- **Buttons (click):** `+500` / `-500` particles, `cull ON/OFF`, `churn` (replace 0 / 25 / 100 objects per frame: exercises the arena free-lists), `clear`.
- **HUD:** FPS, frame and draw time, objects / drawn / culled, arena in use / peak / held / cap.

The window can be resized: it keeps 4:3 and the mouse stays matched to the picture (see `ddSDLInput.h`).

## Build and run
```
brew install sdl2 cmake            # macOS (Linux: your package manager's SDL2 + cmake)
cmake -S . -B build
cmake --build build -j
build/dd_demo
```
LovyanGFX is looked up next to this repository (`../../../LovyanGFX`). Elsewhere: `-DLGFX_ROOT=/path/to/LovyanGFX`.
`-DDD_DEBUG=OFF` builds without the float printf helpers. Use `-DCMAKE_BUILD_TYPE=Release` for realistic timings.

## Files
- `main.cpp`: the demo (`setup()` / `loop()`).
- `sdl_main.cpp`: LovyanGFX's SDL entry point.
