// CYD (ESP32-2432S028R) pins and screen size. Display pins live in platformio.ini
// because TFT_eSPI reads them as build flags.
#pragma once

#include <stdint.h>

constexpr int SCREEN_W = 320;  // landscape
constexpr int SCREEN_H = 240;

// XPT2046 touch controller, on its own SPI bus (separate from the display)
constexpr uint8_t XPT_IRQ  = 36;
constexpr uint8_t XPT_MOSI = 32;
constexpr uint8_t XPT_MISO = 39;
constexpr uint8_t XPT_CLK  = 25;
constexpr uint8_t XPT_CS   = 33;

// Backlight, PWM dimmed
constexpr uint8_t PIN_BACKLIGHT = 21;
