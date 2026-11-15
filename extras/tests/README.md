# dd tests (desktop)

```
cmake -S . -B build
cmake --build build -j
build/dd_tests            # or: ctest --test-dir build
```
Needs SDL2 (macOS: `brew install sdl2`; On current Homebrew `sdl2` is the *sdl2-compat* layer, which runs SDL2 programs on top of SDL3: if the program complains it cannot find libSDL3, run `brew install sdl3` as well.) and a LovyanGFX checkout (default `../../../LovyanGFX`, or `-DLGFX_ROOT=...`). No window opens: rendering tests draw into memory sprites.
`-DCMAKE_BUILD_TYPE=Release` gives realistic numbers for the stress section.

Sections: arena, scene / picking / groups, rendering (pixels, culling, pens, sprites), interaction, SDL input scaling, geometry (hand-written cases plus randomised checks
that every reported contact lies on both shapes), the axis-aligned box, shape-to-shape collision (checked against a separating-axis implementation and closed-form
circle / rectangle formulas), the hardware demo scene (`examples/*/src/DemoScene.h`, run headless at the three display sizes the examples use, with a fake `Arduino.h` from `shim/`, plus a check that the two copies stay identical), and a 6,000-object stress run with churn.
