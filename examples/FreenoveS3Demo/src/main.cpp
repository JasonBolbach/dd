// dd hardware demo: Freenove ESP32-S3-WROOM (8 MB PSRAM) + 480x320 touch LCD.
//
// 1. include/LGFX_User.h holds the LovyanGFX display class with the touch driver (class name LGFX), include/Pins.h the pin map.
//    Replace both for a different board.
// 2. LCD_W / LCD_H below must match the display class in landscape (the demo checks and warns over serial if they do not).
// 3. Upload, open the serial monitor (115200) for the log lines.
//
// Touch is read through SuperGUI's SuperTouch (debounced touch state and gesture recognition), which dd itself does not depend on:
//   drag a shape       move it (dd's ddInteract does the picking and dragging). The dragged shape shows a dashed box and
//                      red crosses where it touches other shapes (dd's shape-to-shape collision).
//   tap a button       +50 / -50 particles, cull on/off
//   medium press       (held 0.2 - 0.8 s) on a shape: bring it to the front
//   long press         (held > 0.8 s) on a shape: reset its angle and scale
//   double click       anywhere: toggle culling
#include <Arduino.h>
#include "LGFX_User.h"
#include <SuperTouch.h>   // from SuperGUI: https://github.com/JasonBolbach/SuperGUI
#include <dd.h>
#include "DemoScene.h"

// Debug output goes to the HARDWARE UART0 (TX = GPIO43, RX = GPIO44 on the Freenove board, i.e. the USB-serial chip's port), never to
// the native USB CDC port: Serial0 is always UART0 whatever ARDUINO_USB_CDC_ON_BOOT says. Change DBG to Serial to follow that setting.
#define DBG Serial0

// ---------------- OPTIONS: edit these ----------------
constexpr int  LCD_W = 480;             // display width in landscape (pixels)
constexpr int  LCD_H = 320;             // display height in landscape (pixels)
constexpr int  SPRITE_DEPTH = 16;       // colour depth of the off-screen buffer: 16 = RGB565 (the buffer is LCD_W x LCD_H x 2 bytes, held in PSRAM)
constexpr bool USE_TOUCH = true;        // false = no touch (the scene still animates)
// Gesture timing lives in SuperTouch (touch.Click1_MAX / Click2_MAX / ClickD_MAX / Radius ...); the defaults are used here.
constexpr uint32_t LOG_EVERY_MS = 5000; // serial status line interval
// ------------------------------------------------------

LGFX        lcd;
LGFX_Sprite canvas(&lcd);
DemoScene   demo;

// Touch source for SuperTouch: LovyanGFX's own getTouch (works with whatever touch driver your LGFX class sets).
static bool ReadTouch(int16_t &x, int16_t &y) {
  int32_t tx, ty;
  if (!lcd.getTouch(&tx, &ty)) return false;
  x = (int16_t)tx; y = (int16_t)ty;
  return true;
}
SuperTouch touch(ReadTouch);

static size_t freeHeap() { return ESP.getFreeHeap(); }       // internal RAM

void setup() {
  DBG.begin(115200);
  lcd.init();
  lcd.fillScreen(TFT_BLACK);
  DBG.printf("display %d x %d, PSRAM %u bytes\n", (int)lcd.width(), (int)lcd.height(), (unsigned)ESP.getPsramSize());
  if (lcd.width() != LCD_W || lcd.height() != LCD_H)
    DBG.printf("WARNING: the display class reports %d x %d but LCD_W x LCD_H is %d x %d. Fix the constants (or the display class).\n",
               (int)lcd.width(), (int)lcd.height(), LCD_W, LCD_H);
  if (!psramFound()) DBG.println("WARNING: no PSRAM. This module needs psram_type = opi / memory_type = qio_opi in platformio.ini; the buffer will use internal RAM and may not fit.");

  canvas.setPsram(psramFound());                  // pixel buffer in PSRAM when the board has it
  canvas.setColorDepth(SPRITE_DEPTH);
  if (!canvas.createSprite(LCD_W, LCD_H)) { DBG.println("ERROR: could not allocate the off-screen buffer."); while (true) delay(1000); }

  demo.FreeHeap = freeHeap;
  size_t heap0 = ESP.getFreeHeap(), ps0 = ESP.getFreePsram();
  demo.Begin(&canvas, LCD_W, LCD_H);
  DBG.printf("scene built: %d objects, arena %u bytes, internal RAM used %u, PSRAM used %u\n", demo.Scene->Count(),
             (unsigned)demo.Scene->Arena()->InUse(), (unsigned)(heap0 - ESP.getFreeHeap()), (unsigned)(ps0 - ESP.getFreePsram()));
}

void loop() {
  if (USE_TOUCH) {
    touch.Poll();                                                    // debounced touch state + gesture recognition (rate limited inside)
    SuperTouchEvent ev;
    while (touch.GetEvent(ev)) {                                     // the extra gestures; drag / tap / select are handled by dd's ddInteract in Frame()
      bool click = ev.Type == ST_SHORT_CLICK || ev.Type == ST_MEDIUM_CLICK || ev.Type == ST_LONG_CLICK;
      if (!click) continue;
      if (ev.Double) demo.DoubleClick(ev.X, ev.Y);
      else if (ev.Type == ST_MEDIUM_CLICK) demo.MediumPress(ev.X, ev.Y);
      else if (ev.Type == ST_LONG_CLICK) demo.LongPress(ev.X, ev.Y);
    }
  }
  demo.Frame(USE_TOUCH && touch.IsTouched(), (float)touch.GetX(), (float)touch.GetY());   // SuperTouch's debounced finger state drives ddInteract
  canvas.pushSprite(0, 0);

  static uint32_t lastLog = 0;
  if (millis() - lastLog >= LOG_EVERY_MS) {
    lastLog = millis();
    ddArena *A = demo.Scene->Arena();
    DBG.printf("frame %d.%d ms  draw %d.%d ms  drawn %u culled %u  arena %uK (peak %uK, cap %uK)  heap %uK  psram %uK\n",
               (int)demo.EmaFrameMs, (int)(demo.EmaFrameMs * 10) % 10, (int)demo.EmaDrawMs, (int)(demo.EmaDrawMs * 10) % 10,
               (unsigned)demo.R->Drawn, (unsigned)demo.R->Culled, (unsigned)(A->InUse() / 1024), (unsigned)(A->Peak() / 1024), (unsigned)(A->Max() / 1024),
               (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024));
  }
}
