// Colors and spacing from SPEC.md 6.1. Wrap colors in lv_color_hex() at use.
#pragma once

#include <stdint.h>

namespace theme {

constexpr uint32_t BG      = 0x000000;
constexpr uint32_t CARD    = 0x111318;
constexpr uint32_t TEXT    = 0xF2F2F5;
constexpr uint32_t MUTED   = 0x9AA0B4;
constexpr uint32_t DIVIDER = 0x23262F;
constexpr uint32_t SOLAR   = 0xF0457E;
constexpr uint32_t AC      = 0x7C88F7;
constexpr uint32_t CHARGE  = 0x3FCF8E;
constexpr uint32_t GEN     = 0xFF8A3D;
constexpr uint32_t WARN    = 0xF5A524;
constexpr uint32_t ALARM   = 0xFF5A5F;

constexpr int PAD        = 12;  // screen edge
constexpr int GAP        = 8;   // between blocks
constexpr int CARD_PAD   = 8;   // inside cards
constexpr int RADIUS     = 8;
constexpr int NAV_H      = 40;  // bottom bar

}  // namespace theme
