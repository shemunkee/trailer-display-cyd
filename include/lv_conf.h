// LVGL 9.2 config, trimmed for the CYD (no PSRAM). Anything not set here
// takes LVGL's default from lv_conf_internal.h.
#ifndef LV_CONF_H
#define LV_CONF_H  // LVGL checks this name to confirm the file was found

// Color: RGB565. The flush callback swaps bytes for the panel.
#define LV_COLOR_DEPTH 16

// Memory: LVGL's own heap for widgets and styles
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_MEM_SIZE (56U * 1024U)  // five screens built at boot; the boot log prints how much is used. 64 KB overflows static DRAM.

#define LV_USE_OS LV_OS_NONE
#define LV_DEF_REFR_PERIOD 33
#define LV_DPI_DEF 130

// Software renderer only, no extra layers or effects we don't use
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_COMPLEX 1   // needed for rounded cards and the SOC ring
#define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NONE

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_SYSMON 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

// Fonts: only the spec type scale (10/12/14/18) plus 40 for the SOC hero
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_24 1  // SOC ring (Overview), MPPT watts (Solar)
#define LV_FONT_MONTSERRAT_36 1  // SOC on the Battery screen
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

// Widgets we won't need on this device
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_TEXTAREA 0
#define LV_USE_ROLLER 0
#define LV_USE_DROPDOWN 0
#define LV_USE_TABLE 0
#define LV_USE_SPAN 0
#define LV_USE_LED 0
#define LV_USE_SCALE 0
#define LV_USE_ANIMIMG 0

// QR codes for the setup screen
#define LV_USE_QRCODE 1

#define LV_BUILD_EXAMPLES 0

#endif  // LV_CONF_H
