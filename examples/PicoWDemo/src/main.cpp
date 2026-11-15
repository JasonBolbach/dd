// dd hardware demo: Raspberry Pi Pico W (RP2040) or Pico 2 W (RP2350) + 320x240 SPI LCD + rotary encoder + two buttons.
//
// 1. The display class is include/LGFX_User_TE004.h (class LGFX_TE004, the TE004 board). Replace it for another display.
// 2. All GPIO numbers are in include/Pins.h; the build stops if two parts share a pin or a pin is not usable.
// 3. LCD_W / LCD_H below must match the display class in landscape (the demo checks and warns over serial if they do not).
// 4. Upload, open the serial monitor (115200, the Pico's USB port) for the log lines.
//
// There is no touch on this board, so the shapes are driven with the encoder and buttons:
//   Encoder turn   in "select" mode: choose a shape. In the other modes: change the selected shape.
//   ButtonA        next mode: select -> move x -> move y -> rotate -> scale -> select ...
//   ButtonB        reset the selected shape's angle and scale (with nothing selected: culling on/off)
// The selected shape shows a dashed box and red crosses where it touches other shapes (dd's shape-to-shape collision).
#include <Arduino.h>
#include "Pins.h"
#include "LGFX_User_TE004.h"
#include <dd.h>
#include "DemoScene.h"

// Debug output goes to the USB serial port (`Serial`). The hardware UART (Serial1) is GP0 / GP1, which are the encoder pins here.
#define DBG Serial

// ---------------- OPTIONS: edit these (pins are in Pins.h) ----------------
constexpr int  LCD_W = 320;             // display width in landscape (pixels)
constexpr int  LCD_H = 240;             // display height in landscape (pixels)
#ifndef DEMO_SPRITE_DEPTH
#define DEMO_SPRITE_DEPTH 8             // set per board in platformio.ini: 8 = RGB332 (LCD_W x LCD_H bytes), 16 = RGB565 (twice that)
#endif
constexpr int  SPRITE_DEPTH = DEMO_SPRITE_DEPTH;
constexpr bool BTN_ACTIVE_LOW = true;   // buttons wired to GND with the internal pull-up
constexpr bool USE_ENCODER = true;      // false = no encoder fitted: its pins are not touched
constexpr bool USE_BUTTONS = true;      // false = no buttons fitted: nothing is read from their pins
constexpr uint32_t LOG_EVERY_MS = 5000; // serial status line interval
// Encoder turns the wrong way? Swap PIN_ENC_A and PIN_ENC_B in Pins.h.
// ----------------------------------------------------

LGFX_TE004  lcd;
LGFX_Sprite canvas(&lcd);
DemoScene   demo;

// ---- a small quadrature encoder (interrupt driven; 4 transitions per detent) ----
class Encoder {
 public:
  void Begin(int a, int b) {
    pa = a; pb = b; inst = this;
    pinMode(pa, INPUT_PULLUP); pinMode(pb, INPUT_PULLUP);
    last = (uint8_t)((digitalRead(pa) << 1) | digitalRead(pb));
    attachInterrupt(digitalPinToInterrupt(pa), isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(pb), isr, CHANGE);
  }
  int Take() { noInterrupts(); int v = ticks; ticks = 0; interrupts(); return v; }     // detents since the last call (+ / -)
 private:
  static void isr() { if (inst) inst->Poll(); }
  void Poll() {
    static const int8_t T[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
    uint8_t s = (uint8_t)((digitalRead(pa) << 1) | digitalRead(pb));
    acc += T[(last << 2) | s]; last = s;
    if (acc >= 4) { ticks++; acc = 0; } else if (acc <= -4) { ticks--; acc = 0; }
  }
  inline static Encoder *inst = nullptr;
  int pa = 0, pb = 0; volatile int ticks = 0; int8_t acc = 0; uint8_t last = 0;
};

// ---- a debounced push button (polled once per frame) ----
class Button {
 public:
  void Begin(int p, bool activeLow) { pin = p; low = activeLow; pinMode(pin, activeLow ? INPUT_PULLUP : INPUT); }
  bool Pressed() {                                                   // true once per press
    bool raw = digitalRead(pin) == (low ? LOW : HIGH);
    uint32_t now = millis();
    if (raw != state && now - changed > 25) { state = raw; changed = now; if (state) return true; }
    return false;
  }
 private:
  int pin = 0; bool low = true, state = false; uint32_t changed = 0;
};

Encoder enc;
Button  btnA, btnB;

static size_t freeHeap() { return rp2040.getFreeHeap(); }

void setup() {
  DBG.begin(115200);
  for (uint32_t t = millis(); !DBG && millis() - t < 3000; ) delay(10);   // USB serial: wait (up to 3 s) for the monitor so the startup lines are not lost
  lcd.init();
  lcd.fillScreen(TFT_BLACK);
  DBG.printf("display %d x %d, heap %u bytes (total %u)\n", (int)lcd.width(), (int)lcd.height(), (unsigned)rp2040.getFreeHeap(), (unsigned)rp2040.getTotalHeap());
  if (lcd.width() != LCD_W || lcd.height() != LCD_H)
    DBG.printf("WARNING: the display class reports %d x %d but LCD_W x LCD_H is %d x %d. Fix the constants (or the display class).\n",
               (int)lcd.width(), (int)lcd.height(), LCD_W, LCD_H);

  if (USE_ENCODER) enc.Begin(PIN_ENC_A, PIN_ENC_B);
  if (USE_BUTTONS) { btnA.Begin(PIN_BTN_A, BTN_ACTIVE_LOW); btnB.Begin(PIN_BTN_B, BTN_ACTIVE_LOW); }

  canvas.setColorDepth(SPRITE_DEPTH);
  if (!canvas.createSprite(LCD_W, LCD_H)) { DBG.println("ERROR: could not allocate the off-screen buffer (try SPRITE_DEPTH 8)."); while (true) delay(1000); }
  DBG.printf("off-screen buffer %d x %d at %d bits = %u bytes, heap left %u\n", LCD_W, LCD_H, SPRITE_DEPTH,
             (unsigned)((size_t)LCD_W * LCD_H * SPRITE_DEPTH / 8), (unsigned)rp2040.getFreeHeap());

  demo.FreeHeap = freeHeap;
  size_t heap0 = rp2040.getFreeHeap();
  demo.Begin(&canvas, LCD_W, LCD_H);
  DBG.printf("scene built: %d objects, arena %u bytes, heap used %u, heap left %u\n", demo.Scene->Count(),
             (unsigned)demo.Scene->Arena()->InUse(), (unsigned)(heap0 - rp2040.getFreeHeap()), (unsigned)rp2040.getFreeHeap());
  demo.EncoderTurn(0);
}

void loop() {
  if (USE_ENCODER) demo.EncoderTurn(enc.Take());
  if (USE_BUTTONS) { if (btnA.Pressed()) demo.ButtonA(); if (btnB.Pressed()) demo.ButtonB(); }

  demo.Frame(false, 0, 0);                      // no touch on this board
  canvas.pushSprite(0, 0);

  static uint32_t lastLog = 0;
  if (millis() - lastLog >= LOG_EVERY_MS) {
    lastLog = millis();
    ddArena *A = demo.Scene->Arena();
    DBG.printf("frame %d.%d ms  draw %d.%d ms  drawn %u culled %u  arena %uK (peak %uK, cap %uK)  heap %uK free\n",
               (int)demo.EmaFrameMs, (int)(demo.EmaFrameMs * 10) % 10, (int)demo.EmaDrawMs, (int)(demo.EmaDrawMs * 10) % 10,
               (unsigned)demo.R->Drawn, (unsigned)demo.R->Culled, (unsigned)(A->InUse() / 1024), (unsigned)(A->Peak() / 1024), (unsigned)(A->Max() / 1024),
               (unsigned)(rp2040.getFreeHeap() / 1024));
  }
}
