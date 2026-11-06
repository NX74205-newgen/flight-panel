#pragma once
#include <Arduino.h>

// All settings live in flash (NVS) and are edited on the web page http://flightpanel.local
// The values below are only first-boot defaults.
struct Settings {
  float lat = 43.2380f;           // observation point (your window); default: central Almaty
  float lon = 76.9450f;
  float radiusKm = 12.0f;         // show a flight when it is closer than this
  float areaKm = 40.0f;           // FR24 polling area (flight counter and "nearest" on the clock screen)
  int maxAltM = 0;                // 0 = no altitude limit
  int brightness = 60;            // 1..255
  int nightBrightness = 8;        // 0 = turn the panel off at night
  int nightStart = 23;            // hour night mode starts
  int nightEnd = 7;               // hour night mode ends
  int pollSec = 8;                // FR24 polling period
  int cycleSec = 8;               // seconds per flight when several are in range
  bool metric = true;             // metres/km or feet/miles
  int viewHeading = 0;            // direction the window faces, deg (top of the plane icon = view from
                                  // the window; 0 = north-up)
  bool flip = true;               // true = rotate the image 180° (USB port on the left)
  char tz[40] = "<+05>-5";        // POSIX TZ (Almaty is UTC+5 since 1 March 2024)
};

extern Settings cfg;

void settingsLoad();
void settingsSave();
