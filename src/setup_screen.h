// Setup screen (SPEC.md 6.9): QR codes to join the setup WiFi and open the page.
#pragma once

// name: this display's name for the title ("Setup: Sabrina"), or "" before it's named
void setup_screen_create(const char* name, const char* ssid, const char* pass, const char* url, void (*onCancel)());
void setup_screen_status(const char* msg);  // replaces the title, e.g. "Saved, restarting"
