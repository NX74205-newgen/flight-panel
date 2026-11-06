#include "flights.h"
#include "settings.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <algorithm>

static FlightSnapshot snap;
static SemaphoreHandle_t mtx = nullptr;
static FlightSnapshot fresh;  // static: keeps 2+ KB off the stack

void flightsInit() { if (!mtx) mtx = xSemaphoreCreateMutex(); }

static constexpr float DEG2RAD = M_PI / 180.0f;

float haversineKm(float lat1, float lon1, float lat2, float lon2) {
  float dLat = (lat2 - lat1) * DEG2RAD, dLon = (lon2 - lon1) * DEG2RAD;
  float a = sinf(dLat / 2) * sinf(dLat / 2) +
            cosf(lat1 * DEG2RAD) * cosf(lat2 * DEG2RAD) * sinf(dLon / 2) * sinf(dLon / 2);
  return 6371.0f * 2 * atan2f(sqrtf(a), sqrtf(1 - a));
}

float bearingDeg(float lat1, float lon1, float lat2, float lon2) {
  float y = sinf((lon2 - lon1) * DEG2RAD) * cosf(lat2 * DEG2RAD);
  float x = cosf(lat1 * DEG2RAD) * sinf(lat2 * DEG2RAD) -
            sinf(lat1 * DEG2RAD) * cosf(lat2 * DEG2RAD) * cosf((lon2 - lon1) * DEG2RAD);
  float b = atan2f(y, x) / DEG2RAD;
  return b < 0 ? b + 360 : b;
}

static void copyStr(char *dst, size_t n, JsonVariantConst v) {
  const char *s = v.as<const char *>();
  strlcpy(dst, s ? s : "", n);
}

static void markFailed(int code) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  snap.ok = false;
  snap.lastHttp = code;
  xSemaphoreGive(mtx);
}

bool flightsFetch() {
  if (WiFi.status() != WL_CONNECTED) return false;

  // bounding box around the observation point
  float dLat = cfg.areaKm / 111.0f;
  float dLon = cfg.areaKm / (111.0f * cosf(cfg.lat * DEG2RAD));
  char url[300];
  snprintf(url, sizeof(url),
           "https://data-cloud.flightradar24.com/zones/fcgi/feed.js?bounds=%.3f,%.3f,%.3f,%.3f"
           "&faa=1&satellite=1&mlat=1&flarm=1&adsb=1&gnd=0&air=1&vehicles=0&estimated=0"
           "&maxage=14400&gliders=0&stats=0",
           cfg.lat + dLat, cfg.lat - dLat, cfg.lon - dLon, cfg.lon + dLon);

  WiFiClientSecure client;
  client.setInsecure();  // public read-only data; the certificate is not verified
  HTTPClient http;
  http.setTimeout(8000);
  http.setConnectTimeout(6000);
  http.useHTTP10(true);  // no chunked encoding, so JSON can be parsed straight from the stream
  if (!http.begin(client, url)) return false;
  http.setUserAgent("Mozilla/5.0 (X11; Linux x86_64) FlightPanel/1.0");
  http.addHeader("Accept", "application/json");

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    markFailed(code);
    log_w("FR24 HTTP %d", code);
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    log_w("JSON: %s", err.c_str());
    markFailed(code);
    return false;
  }

  fresh = FlightSnapshot();
  fresh.lastHttp = code;
  // Entry format: [icao, lat, lon, track, alt, speed, squawk, radar, type, reg, ts,
  //                from, to, flight, on_ground, vspeed, callsign, glider, airline]
  // Other top-level keys ("full_count", "version", ...) are not arrays and are skipped.
  Flight f;
  for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
    JsonArrayConst a = kv.value().as<JsonArrayConst>();
    if (a.isNull() || a.size() < 17) continue;

    f = Flight();
    strlcpy(f.id, kv.key().c_str(), sizeof(f.id));
    f.lat = a[1] | 0.0f;
    f.lon = a[2] | 0.0f;
    f.track = a[3] | 0;
    f.altFt = a[4] | 0;
    f.speedKt = a[5] | 0;
    copyStr(f.type, sizeof(f.type), a[8]);
    copyStr(f.reg, sizeof(f.reg), a[9]);
    f.ts = a[10] | 0;
    copyStr(f.from, sizeof(f.from), a[11]);
    copyStr(f.to, sizeof(f.to), a[12]);
    copyStr(f.flight, sizeof(f.flight), a[13]);
    f.onGround = (a[14] | 0) != 0;
    f.vspeedFpm = a[15] | 0;
    copyStr(f.callsign, sizeof(f.callsign), a[16]);
    copyStr(f.airline, sizeof(f.airline), a[18]);

    if (f.onGround) continue;
    if (cfg.maxAltM > 0 && f.altFt * 0.3048f > cfg.maxAltM) continue;

    f.distKm = haversineKm(cfg.lat, cfg.lon, f.lat, f.lon);
    f.bearing = bearingDeg(cfg.lat, cfg.lon, f.lat, f.lon);
    if (f.distKm > cfg.areaKm * 1.5f) continue;

    if (fresh.count < MAX_FLIGHTS) {
      fresh.list[fresh.count++] = f;
    } else {
      // list is full (busy sky): keep the nearest ones, replace the farthest if this one is closer
      Flight *far = std::max_element(fresh.list, fresh.list + MAX_FLIGHTS,
                                     [](const Flight &x, const Flight &y) { return x.distKm < y.distKm; });
      if (f.distKm < far->distKm) *far = f;
    }
  }

  std::sort(fresh.list, fresh.list + fresh.count,
            [](const Flight &a, const Flight &b) { return a.distKm < b.distKm; });
  fresh.inRadius = 0;
  while (fresh.inRadius < fresh.count && fresh.list[fresh.inRadius].distKm <= cfg.radiusKm)
    fresh.inRadius++;
  fresh.ok = true;
  fresh.fetchedAt = millis();

  xSemaphoreTake(mtx, portMAX_DELAY);
  snap = fresh;
  xSemaphoreGive(mtx);
  log_i("FR24: %d flights, %d in radius", fresh.count, fresh.inRadius);
  return true;
}

void flightsGet(FlightSnapshot &out) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  out = snap;
  xSemaphoreGive(mtx);
}
