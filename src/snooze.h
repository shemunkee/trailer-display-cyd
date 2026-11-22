// Snooze (SPEC.md 6.10): after the snooze time with no touch, flash the logo
// for 1 s, then turn the backlight off. A tap wakes it; that tap is swallowed
// so it never presses a button on the screen underneath.
#pragma once

void snooze_begin();  // after the first screen is created
void snooze_wake();   // e.g. for alarms (SPEC 6.11)
