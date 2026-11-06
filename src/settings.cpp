#include "settings.h"
#include <Preferences.h>

Settings cfg;

static Preferences prefs;

void settingsLoad() {
  prefs.begin("panel", false);  // read-write: creates the namespace on first boot
  if (!prefs.isKey("lat")) {     // first boot: persist the defaults
    prefs.end();
    settingsSave();
    return;
  }
  cfg.lat = prefs.getFloat("lat", cfg.lat);
  cfg.lon = prefs.getFloat("lon", cfg.lon);
  cfg.radiusKm = prefs.getFloat("radius", cfg.radiusKm);
  cfg.areaKm = prefs.getFloat("area", cfg.areaKm);
  cfg.maxAltM = prefs.getInt("maxalt", cfg.maxAltM);
  cfg.brightness = prefs.getInt("bri", cfg.brightness);
  cfg.nightBrightness = prefs.getInt("nbri", cfg.nightBrightness);
  cfg.nightStart = prefs.getInt("nstart", cfg.nightStart);
  cfg.nightEnd = prefs.getInt("nend", cfg.nightEnd);
  cfg.pollSec = prefs.getInt("poll", cfg.pollSec);
  cfg.cycleSec = prefs.getInt("cycle", cfg.cycleSec);
  cfg.metric = prefs.getBool("metric", cfg.metric);
  cfg.flip = prefs.getBool("flip", cfg.flip);
  cfg.viewHeading = prefs.getInt("view", cfg.viewHeading);
  if (prefs.isKey("tz")) prefs.getString("tz", cfg.tz, sizeof(cfg.tz));
  prefs.end();

  // sanitize whatever came from flash or the web form
  cfg.lat = constrain(cfg.lat, -90.0f, 90.0f);
  cfg.lon = constrain(cfg.lon, -180.0f, 180.0f);
  cfg.radiusKm = constrain(cfg.radiusKm, 1.0f, 100.0f);
  cfg.areaKm = constrain(cfg.areaKm, cfg.radiusKm, 200.0f);
  cfg.maxAltM = max(cfg.maxAltM, 0);
  cfg.brightness = constrain(cfg.brightness, 1, 255);
  cfg.nightBrightness = constrain(cfg.nightBrightness, 0, 255);
  cfg.nightStart = constrain(cfg.nightStart, 0, 23);
  cfg.nightEnd = constrain(cfg.nightEnd, 0, 23);
  cfg.pollSec = constrain(cfg.pollSec, 5, 120);
  cfg.cycleSec = constrain(cfg.cycleSec, 3, 60);
  cfg.viewHeading = ((cfg.viewHeading % 360) + 360) % 360;
}

void settingsSave() {
  prefs.begin("panel", false);
  prefs.putFloat("lat", cfg.lat);
  prefs.putFloat("lon", cfg.lon);
  prefs.putFloat("radius", cfg.radiusKm);
  prefs.putFloat("area", cfg.areaKm);
  prefs.putInt("maxalt", cfg.maxAltM);
  prefs.putInt("bri", cfg.brightness);
  prefs.putInt("nbri", cfg.nightBrightness);
  prefs.putInt("nstart", cfg.nightStart);
  prefs.putInt("nend", cfg.nightEnd);
  prefs.putInt("poll", cfg.pollSec);
  prefs.putInt("cycle", cfg.cycleSec);
  prefs.putBool("metric", cfg.metric);
  prefs.putBool("flip", cfg.flip);
  prefs.putInt("view", cfg.viewHeading);
  prefs.putString("tz", cfg.tz);
  prefs.end();
}
