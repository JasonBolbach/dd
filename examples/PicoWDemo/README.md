# dd Pico W demo (Raspberry Pi Pico W / Pico 2 W, 320x240 LCD)

The dd scene demo on a Raspberry Pi **Pico W** (RP2040) or **Pico 2 W** (RP2350) with the TE004 board: a 320x240 ST7789 SPI display, a rotary encoder with a push button
and one board button. One of each shape, nested groups (a solar system, a clock, a gear), a field of bouncing particles and a stats HUD.

This board has **no touch**, so the shapes are driven with the encoder and buttons:

| Control | Action |
|---|---|
| Encoder turn | in **select** mode choose a shape; in the other modes change the selected shape (position, angle, scale) |
| ButtonA | next mode: select, move x, move y, rotate, scale, select ... |
| ButtonB | reset the selected shape's angle and scale (with nothing selected: culling on/off) |

The selected shape shows a dashed box and **red crosses where it touches other shapes** (dd's shape-to-shape collision). The HUD shows the mode, how many shapes the selection touches,
frame and draw time, objects drawn / culled, and arena and heap use.

> Status: this example **compiles and links** for both `picow` and `pico2w`. It has not been run on hardware yet; please report anything that behaves differently.

```
pio run -e picow  -t upload      # Pico W
pio run -e pico2w -t upload      # Pico 2 W
```
Open the serial monitor (115200) on the Pico's **USB** port for the log.

## Files
| File | What it holds |
|---|---|
| `include/Pins.h` | every GPIO: display (SPI0), encoder, buttons. Compile-time checks stop the build if two parts share a pin, a pin belongs to the wireless chip (GP23 / 24 / 25), or the SCLK / MOSI pins do not match the SPI port. |
| `include/LGFX_User_TE004.h` | the LovyanGFX display class `LGFX_TE004`: ST7789 240x320 on SPI0 rotated to landscape (320x240), PWM backlight. |
| `src/main.cpp` | hardware only: options, a small interrupt-driven encoder and debounced buttons, the serial log, `setup()` / `loop()`. |
| `src/DemoScene.h` | the demo scene (portable). **Identical to the copy in `../FreenoveS3Demo`.** |

## Using another board or display
Copy `LGFX_User_TE004.h` and `Pins.h`, change the panel class and the numbers, set `LCD_W` / `LCD_H` at the top of `main.cpp` to the landscape size (the demo warns over serial if they do not match the display class).
Encoder turns the wrong way? Swap `PIN_ENC_A` and `PIN_ENC_B` in `Pins.h`. GP0 / GP1 (the encoder here) are also UART0, so `Serial1` is not available for debug output with this wiring.

## Memory: why the two environments differ
The off-screen buffer is the big allocation (`LCD_W x LCD_H x bytes per pixel`). The Pico W has 264 KB of RAM (about 71 KB is taken by the program before anything runs), the Pico 2 W has 520 KB.

| Environment | Buffer | Particles | Scene arena cap |
|---|---|---|---|
| `picow` | 8-bit (RGB332), 77 KB | 40 | 48 KB |
| `pico2w` | 16-bit (RGB565), 154 KB | 150 | 128 KB |

(Set in `platformio.ini` through `DEMO_SPRITE_DEPTH`, `DEMO_PARTICLES`, `DEMO_ARENA_BYTES`.) dd colours are RGB565 either way; LovyanGFX converts them to the 8-bit buffer, so 8-bit colour looks a little coarser.
The startup log prints the buffer size, how much heap the scene took and how much is left: if heap runs out, lower `DEMO_PARTICLES` or `DEMO_ARENA_BYTES`, or use `SPRITE_DEPTH 8`.

## Options (top of `src/main.cpp`)
- `LCD_W`, `LCD_H`: display size. `SPRITE_DEPTH`: buffer colour depth (8 or 16).
- `USE_ENCODER`, `USE_BUTTONS`: set `false` for a part that is not fitted (its pins are then never read). `BTN_ACTIVE_LOW`: buttons wired to GND with the internal pull-up.

## Things to check on real hardware
- The RP2040 has no FPU, so float maths is done in software (the pico-sdk's optimised routines); expect fewer particles than on the RP2350 or ESP32-S3. Watch `draw` in the log and lower `DEMO_PARTICLES` if the frame rate is poor.
- Encoder detents: the decoder counts 4 transitions per detent; if your encoder gives 2 per detent change `4` in `Encoder::Poll()`.
