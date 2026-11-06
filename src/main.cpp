// Flight Panel — flights passing your window on a 64x32 HUB75 panel driven by an Adafruit MatrixPortal S3.
//
// On-board buttons:
//   UP   — cycle brightness (5 steps)
//   DOWN — next flight; hold for 5 s to forget Wi-Fi and start the setup access point

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <time.h>
#include "settings.h"
#include "flights.h"
#include "display.h"
#include "webui.h"

static constexpr int PIN_BTN_UP = 6;
static constexpr int PIN_BTN_DOWN = 7;
static constexpr const char *HOSTNAME = "flightpanel";
static constexpr const char *AP_NAME = "FlightPanel-Setup";

static void wifiResetAndRestart() {
  WiFiManager wm;
  wm.resetSettings();
  delay(200);
  ESP.restart();
}

static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFiManager wm;
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(0);  // wait for configuration indefinitely
  wm.setTitle("Flight Panel");
  wm.setAPCallback([](WiFiManager *) {
    Serial.printf("[wifi] setup portal: join '%s' and open 192.168.4.1\n", AP_NAME);
    displaySetMode(UiMode::Portal);
  });
  wm.setSaveConfigCallback([] { displaySetMode(UiMode::Connecting, "saved"); });

  if (!WiFi.SSID().isEmpty()) displaySetMode(UiMode::Connecting, WiFi.SSID().c_str());
  if (!wm.autoConnect(AP_NAME)) ESP.restart();
  WiFi.setSleep(false);  // snappier web page and OTA
  WiFi.setAutoReconnect(true);
}

static void handleButtons() {
  static uint32_t downSince = 0;
  static bool upPrev = true, downPrev = true, longFired = false;
  bool up = digitalRead(PIN_BTN_UP), down = digitalRead(PIN_BTN_DOWN);  // active low

  if (!up && upPrev) {
    static const int steps[] = {10, 30, 60, 120, 220};
    int i = 0;
    while (i < 5 && steps[i] <= cfg.brightness) i++;
    cfg.brightness = steps[i % 5];
    settingsSave();
    char m[16];
    snprintf(m, sizeof(m), "BRIGHT %d", cfg.brightness);
    displayFlash(m);
  }
  if (!down && downPrev) { downSince = millis(); longFired = false; }
  if (!down && !longFired && millis() - downSince > 5000) {
    longFired = true;
    displayFlash("WIFI RESET", 3000);
    delay(1000);
    wifiResetAndRestart();
  }
  if (down && !downPrev && !longFired && millis() - downSince > 30) displayNextFlight();
  upPrev = up;
  downPrev = down;
}

void setup() {
  Serial.begin(115200);
  delay(1500);  // let USB-CDC come up so the first log lines are not lost
  Serial.printf("\n[boot] Flight Panel, PSRAM %u KB, heap %u KB\n", ESP.getPsramSize() / 1024, ESP.getFreeHeap() / 1024);
  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);

  settingsLoad();
  flightsInit();
  displayInit();
  displaySetMode(UiMode::Boot, "v1.0");
  displayStartTask();
  delay(1200);

  Serial.println("[boot] display ok, connecting Wi-Fi");
  connectWifi();

  configTzTime(cfg.tz, "pool.ntp.org", "time.google.com");
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);

  ArduinoOTA.setHostname(HOSTNAME);
#ifdef OTA_PASSWORD
  ArduinoOTA.setPassword(OTA_PASSWORD);
#endif
  ArduinoOTA.onStart([] { displaySetMode(UiMode::Connecting, "UPDATING..."); });
  ArduinoOTA.begin();

  webuiBegin(wifiResetAndRestart);
  displaySetMode(UiMode::Run);
  Serial.printf("Flight Panel ready: http://%s.local/  (%s)\n", HOSTNAME, WiFi.localIP().toString().c_str());
}

void loop() {
  static uint32_t lastFetch = 0;
  static int fails = 0;
  uint32_t now = millis();

  ArduinoOTA.handle();
  webuiLoop();
  handleButtons();

  // poll FR24; on errors back off exponentially, up to 2 minutes
  uint32_t period = cfg.pollSec * 1000UL;
  if (fails) period = min<uint32_t>(120000UL, period << min(fails, 4));
  if (now - lastFetch >= period || lastFetch == 0) {
    lastFetch = now;
    if (flightsFetch()) fails = 0;
    else fails++;
  }

  // Wi-Fi gone for a long time: reboot (WiFi.setAutoReconnect usually recovers on its own)
  static uint32_t wifiLostAt = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (!wifiLostAt) wifiLostAt = now;
    else if (now - wifiLostAt > 300000) ESP.restart();
  } else {
    wifiLostAt = 0;
  }
  delay(5);
}
