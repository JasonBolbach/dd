# dd ESP32-S3 demo (Freenove ESP32-S3-WROOM, 480x320 touch LCD)

The dd scene demo on a Freenove ESP32-S3-WROOM (8 MB flash, 8 MB PSRAM, both QIO) with a 480x320 touch LCD:
one of each shape, nested groups (a solar system, a clock, a gear), a field of 400 bouncing particles, on-screen buttons and a stats HUD.

**Touch:** drag any shape. The dragged shape shows a dashed box and **red crosses where it touches other shapes** (dd's shape-to-shape collision).
Tap `+50` / `-50` to add or remove particles and `cull on/off` to see what culling saves (`drawn` / `culled` on the HUD).

> Status: this example **compiles and links** for the `esp32s3` environment. It has not been run on hardware yet; please report anything that behaves differently.

## Before you build
1. **Display + touch:** `include/LGFX_User.h` (ST7796 480x320 on SPI2 at 80 MHz with DMA, FT6336 touch on I2C) and `include/Pins.h` describe the
   Freenove ESP32-S3 board. For another board replace them with your own LovyanGFX `class LGFX` (landscape, touch driver set on the panel so
   `lcd.getTouch()` works) and pin map.
2. **Display size:** `LCD_W` and `LCD_H` at the top of `src/main.cpp` (480 x 320). They must match the display class in landscape; on start-up the demo prints a
   warning over serial if they do not.
3. **Library:** `platformio.ini` uses the dd checkout two folders up (`symlink://../..`); switch to the GitHub URL line to build a copy of this folder on its own.

```
pio run -e esp32s3 -t upload
pio device monitor            # 115200
```

`platformio.ini` sets `flash_mode = qio`, `psram_type = opi`, `memory_type = qio_opi` (QIO flash, OCTAL PSRAM: forcing quad PSRAM makes it fail with
"PSRAM chip is not connected, or wrong PSRAM line mode"), the 8 MB partition table, and `-DBOARD_HAS_PSRAM`. The serial log prints the PSRAM size at start.
`ARDUINO_USB_CDC_ON_BOOT=0` sends the log to the UART port (`Serial0`); change `DBG` in `main.cpp` to `Serial` for the native USB port.

## How it is put together
| File | What it holds |
|---|---|
| `src/main.cpp` | hardware only: options, display, touch reading, the serial log, `setup()` / `loop()` |
| `src/DemoScene.h` | the demo scene (portable: Arduino + LovyanGFX + dd). **Identical to the copy in `../PicoWDemo`.** |
| `include/LGFX_User.h`, `include/Pins.h` | the display class and pin map for this board |

The frame loop is `demo.Frame(touching, x, y)` (input, animation, drawing into an off-screen sprite) followed by `canvas.pushSprite(0, 0)`.

## Options
- `LCD_W`, `LCD_H`: display size (`main.cpp`).
- `SPRITE_DEPTH` (`main.cpp`): 16 = RGB565. The off-screen buffer is `LCD_W x LCD_H x 2` bytes (307 KB here) and lives in PSRAM (`canvas.setPsram(...)`).
- `USE_TOUCH`: `false` for no touch (the scene still animates).
- Build flags (`platformio.ini`): `DEMO_PARTICLES` (400), `DEMO_ARENA_BYTES` (512 KB cap for the scene; dd's arena takes PSRAM first), `DEMO_BUTTONS` (1 = on-screen buttons).

## The serial log
At start: display and PSRAM size, then how many objects the scene holds and how much internal RAM / PSRAM it took. Every 5 seconds: frame and draw time, objects
drawn / culled, arena use (and peak / cap), free heap and PSRAM. Use it to size `DEMO_ARENA_BYTES` for your own scene: set the cap a little above the peak.

## Things to check on real hardware
- Frame rate with 400 particles. If it is too slow lower `DEMO_PARTICLES`, or leave culling on and keep most of the field off-screen.
- `lcd.getTouch()` coordinates are in landscape display pixels here; if your touch panel needs calibration or axis swapping do that in the display class.
