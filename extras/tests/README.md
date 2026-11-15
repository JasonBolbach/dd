# dd tests (desktop)

```
cmake -S . -B build
cmake --build build -j
build/dd_tests            # or: ctest --test-dir build
```
Needs SDL2 and a LovyanGFX checkout (default `../../../LovyanGFX`, or `-DLGFX_ROOT=...`). No window opens: rendering tests draw into memory sprites.
`-DCMAKE_BUILD_TYPE=Release` gives realistic numbers for the stress section.

Sections: arena, scene / picking / groups, rendering (pixels, culling, pens, sprites), interaction, SDL input scaling, geometry (hand-written cases plus randomised checks
that every reported contact lies on both shapes), the axis-aligned box, shape-to-shape collision (checked against a separating-axis implementation and closed-form
circle / rectangle formulas), and a 6,000-object stress run with churn.
