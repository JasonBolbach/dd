#pragma once
// Minimal stand-in for Arduino.h so the hardware demo scene (examples/*/src/DemoScene.h) can be exercised on the desktop.
// Time is advanced by hand (fakeNowUs() += ...), so tests are deterministic.
#include <cstdint>
inline uint32_t &fakeNowUs() { static uint32_t t = 0; return t; }
inline uint32_t millis() { return fakeNowUs() / 1000; }
inline uint32_t micros() { return fakeNowUs(); }
