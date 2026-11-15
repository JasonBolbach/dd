// dd hardware demo: Raspberry Pi Pico W (RP2040) or Pico 2 W (RP2350) + 320x240 SPI LCD + rotary encoder + two buttons.
//
// 1. The display class is include/LGFX_User_TE004.h (class LGFX_TE004, the TE004 board). Replace it for another display.
// 2. All GPIO numbers are in include/Pins.h; the build stops if two parts share a pin or a pin is not usable.
// 3. LCD_W / LCD_H below must match the display class in landscape (the demo checks and warns over serial if they do not).
// 4. Upload, open the serial monitor (115200, the Pico's USB port) for the log lines.
//
// There is no touch on this board, so the shapes are driven with the encoder and buttons. They are read through SuperGUI's
// SuperEncoder and SuperButton (which dd itself does not depend on):
//   Encoder turn    "select" mode: choose a shape, one per detent. Other modes: change the selected shape, and the FASTER you turn the
//                   bigger the steps (SuperEncoder's 3-speed acceleration: 1 / 5 / 20 per detent).
//   ButtonA  short  next mode: select -> move x -> move y -> rotate -> scale -> select ...
//            long   back to "select" mode        double click   previous mode
//   ButtonB  short  reset the selected shape's angle and scale (nothing selected: select the first shape)
//            medium (held 0.2 - 0.8 s) bring the selected shape to the front        long   toggle culling
// The selected shape shows a dashed box and red crosses where it touches other shapes (dd's shape-to-shape collision).
#include <Arduino.h>
#include "Pins.h"
#include "LGFX_User_TE004.h"
#include <SuperEncoder.h>   // from SuperGUI: https://github.com/JasonBolbach/SuperGUI
#include <SuperButton.h>
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

SuperEncoder enc(PIN_ENC_A, PIN_ENC_B);                                  // interrupt driven, with acceleration
SuperButton  btnA(PIN_BTN_A, BTN_ACTIVE_LOW), btnB(PIN_BTN_B, BTN_ACTIVE_LOW);   // timer driven: no polling needed

// Encoder step sizes per mode: one shape per detent when choosing, accelerating steps when editing a shape.
static void setEncoderSteps(DemoScene::Mode m) {
  if (m == DemoScene::M_SELECT) { enc.StepSlow = enc.StepMedium = enc.StepFast = 1; }
  else                          { enc.StepSlow = 1; enc.StepMedium = 5; enc.StepFast = 20; }
}

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

  if (USE_ENCODER) enc.Begin();                                              // interrupts + pull-ups
  if (USE_BUTTONS) { const uint8_t m = BTN_ACTIVE_LOW ? INPUT_PULLUP : INPUT; btnA.Begin(m); btnB.Begin(m); }

  canvas.setColorDepth(SPRITE_DEPTH);
  if (!canvas.createSprite(LCD_W, LCD_H)) { DBG.println("ERROR: could not allocate the off-screen buffer (try SPRITE_DEPTH 8)."); while (true) delay(1000); }
  DBG.printf("off-screen buffer %d x %d at %d bits = %u bytes, heap left %u\n", LCD_W, LCD_H, SPRITE_DEPTH,
             (unsigned)((size_t)LCD_W * LCD_H * SPRITE_DEPTH / 8), (unsigned)rp2040.getFreeHeap());

  demo.FreeHeap = freeHeap;
  size_t heap0 = rp2040.getFreeHeap();
  demo.Begin(&canvas, LCD_W, LCD_H);
  DBG.printf("scene built: %d objects, arena %u bytes, heap used %u, heap left %u\n", demo.Scene->Count(),
             (unsigned)demo.Scene->Arena()->InUse(), (unsigned)(heap0 - rp2040.getFreeHeap()), (unsigned)rp2040.getFreeHeap());
  setEncoderSteps(demo.ModeIdx);
}

void loop() {
  if (USE_BUTTONS) {
    uint8_t ev;
    while (btnA.GetEvent(ev)) {
      uint8_t kind = ev & 0x0F;                                     // 1 = short, 2 = medium, 3 = long
      if (ev & SuperButton::SB_DOUBLE) demo.PrevMode();
      else if (kind == 1) demo.NextMode();
      else if (kind == 3) demo.SetMode(DemoScene::M_SELECT);
      setEncoderSteps(demo.ModeIdx);
    }
    while (btnB.GetEvent(ev)) {
      uint8_t kind = ev & 0x0F;
      if (kind == 1) demo.ResetSelected();
      else if (kind == 2) demo.BringSelectedToFront();
      else if (kind == 3) demo.ToggleCull();
    }
  }
  if (USE_ENCODER) demo.EncoderTurn((int)enc.GetDelta());          // speed-scaled steps since the last frame

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
