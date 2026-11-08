// Web UI at http://flightpanel.local/ — settings and the list of flights in the area.
// Bilingual (Russian / English): picked from ?lang=, then the "lang" cookie, then Accept-Language.
// Note: there is no authentication; the page is meant for a trusted home network only.
#include "webui.h"
#include "settings.h"
#include "flights.h"
#include "lookup.h"
#include "display.h"
#include <WebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>

static WebServer server(80);
static FlightSnapshot ws;  // static: too big for the stack

// ---------- UI strings ----------
struct Txt {
  const char *code, *other, *otherLabel;
  const char *inArea, *inRadius, *error, *updated, *secAgo, *heapFree, *kb;
  const char *thFlight, *thRoute, *thType, *thAlt, *thKm, *m;
  const char *settings, *lat, *lon, *radius, *area, *maxAlt, *bri, *nightBri, *nightFrom, *nightTo;
  const char *cycle, *poll, *units, *metric, *imperial, *view, *orient, *usbLeft, *usbRight, *tz;
  const char *save, *reboot, *wifiReset, *wifiConfirm, *ota, *rebooting, *wifiDone;
};

static const Txt RU = {
    "ru", "en", "EN",
    "В зоне", "в радиусе показа", "ошибка", "обновлено", "с назад", "свободно", "КБ",
    "Рейс", "Маршрут", "Тип", "Высота", "км", "м",
    "Настройки", "Широта", "Долгота", "Радиус показа, км", "Зона опроса, км",
    "Макс. высота, м (0 — любая)", "Яркость (1–255)", "Ночная яркость (0 — выкл.)", "Ночь с (час)",
    "Ночь до (час)", "Показ одного рейса, с", "Опрос FR24, с", "Единицы", "метры, км", "футы, мили",
    "Окно смотрит на, ° (0 — север вверху)", "Ориентация", "USB-порт слева", "USB-порт справа",
    "Часовой пояс (POSIX)", "Сохранить", "Перезагрузить", "Сбросить Wi-Fi",
    "Забыть Wi-Fi и включить точку FlightPanel-Setup?", "обновление по воздуху", "Перезагрузка...",
    "Wi-Fi сброшен. Подключитесь к сети FlightPanel-Setup."};

static const Txt EN = {
    "en", "ru", "RU",
    "In area", "within display radius", "error", "updated", "s ago", "free", "KB",
    "Flight", "Route", "Type", "Altitude", "km", "m",
    "Settings", "Latitude", "Longitude", "Display radius, km", "Polling area, km",
    "Max altitude, m (0 = any)", "Brightness (1–255)", "Night brightness (0 = off)", "Night from (hour)",
    "Night until (hour)", "Seconds per flight", "FR24 polling, s", "Units", "metres, km", "feet, miles",
    "Window faces, ° (0 = north-up)", "Orientation", "USB port on the left", "USB port on the right",
    "Time zone (POSIX)", "Save", "Reboot", "Reset Wi-Fi",
    "Forget Wi-Fi and start the FlightPanel-Setup access point?", "over-the-air update", "Rebooting...",
    "Wi-Fi has been reset. Join the FlightPanel-Setup network."};

// Russian if asked explicitly (?lang=ru, remembered in a cookie) or if the browser prefers
// Russian or Kazakh; English otherwise.
static const Txt &lang() {
  String l;
  if (server.hasArg("lang")) {
    l = server.arg("lang");
  } else {
    String c = server.header("Cookie");
    int i = c.indexOf("lang=");
    if (i >= 0) l = c.substring(i + 5, i + 7);
  }
  if (l == "ru") return RU;
  if (l == "en") return EN;
  String al = server.header("Accept-Language");
  al.toLowerCase();
  return (al.startsWith("ru") || al.startsWith("kk")) ? RU : EN;
}

static const char PAGE_HEAD[] PROGMEM = R"HTML(<head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Flight Panel</title>
<style>
:root{--bg:#0f1115;--fg:#e6e6e6;--mut:#8a8f98;--acc:#ffaa00;--card:#181b21;--bd:#2a2f38}
@media (prefers-color-scheme:light){:root{--bg:#f5f6f8;--fg:#1b1d21;--mut:#5d636e;--acc:#c27400;--card:#fff;--bd:#dde1e6}}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,sans-serif}
main{max-width:640px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:4px 0 16px;display:flex;justify-content:space-between;align-items:center}h1 b{color:var(--acc)}
h1 a{font-size:14px;font-weight:600;color:var(--mut);text-decoration:none;border:1px solid var(--bd);border-radius:6px;padding:3px 8px}
.card{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:14px;margin-bottom:14px}
label{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:6px 0}
label span{color:var(--mut)}input,select{flex:none;width:180px;box-sizing:border-box;padding:6px 8px;border-radius:6px;border:1px solid var(--bd);background:var(--bg);color:var(--fg);font:inherit}
button{background:var(--acc);color:#000;border:0;border-radius:6px;padding:9px 16px;font:inherit;font-weight:600;cursor:pointer}
button.sec{background:transparent;color:var(--fg);border:1px solid var(--bd)}
table{width:100%;border-collapse:collapse;font-size:14px}td,th{padding:5px 4px;text-align:left;border-bottom:1px solid var(--bd)}
th{color:var(--mut);font-weight:500}.in{color:var(--acc);font-weight:600}small{color:var(--mut)}
</style></head><body><main>)HTML";

// HTML-escape for both text and attribute values (attributes below are single-quoted).
static String esc(const char *s) {
  String o;
  for (; *s; s++) {
    switch (*s) {
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '&': o += "&amp;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&#39;"; break;
      default: o += *s;
    }
  }
  return o;
}

static void field(String &h, const char *name, const char *label, const String &val, const char *attrs = "") {
  h += "<label><span>"; h += label; h += "</span><input name='"; h += name; h += "' value='";
  h += val; h += "' "; h += attrs; h += "></label>";
}

static void option(String &h, const char *value, bool selected, const char *text) {
  h += "<option value="; h += value; h += selected ? " selected>" : ">"; h += text; h += "</option>";
}

static void handleRoot() {
  const Txt &t = lang();
  if (server.hasArg("lang"))  // remember an explicit choice for a year
    server.sendHeader("Set-Cookie", String("lang=") + t.code + "; Max-Age=31536000; Path=/");
  flightsGet(ws);
  String h;
  h.reserve(10000);
  h += "<!doctype html><html lang="; h += t.code; h += ">";
  h += FPSTR(PAGE_HEAD);
  h += "<h1><span>&#9992; <b>Flight Panel</b></span><a href='/?lang="; h += t.other; h += "'>";
  h += t.otherLabel; h += "</a></h1>";

  h += "<div class=card><b>"; h += t.inArea; h += " "; h += String(cfg.areaKm, 0); h += " "; h += t.thKm; h += ": ";
  h += ws.count;
  h += "</b>, "; h += t.inRadius; h += ": "; h += ws.inRadius;
  h += "<br><small>FR24: ";
  h += ws.ok ? String("OK") : (String(t.error) + " " + ws.lastHttp);
  if (ws.fetchedAt) { h += ", "; h += t.updated; h += " "; h += (millis() - ws.fetchedAt) / 1000; h += " "; h += t.secAgo; }
  h += " · Wi-Fi "; h += esc(WiFi.SSID().c_str()); h += " ("; h += WiFi.RSSI(); h += " dBm)";
  h += " · "; h += t.heapFree; h += " "; h += ESP.getFreeHeap() / 1024; h += " "; h += t.kb; h += "</small>";
  if (ws.count) {
    h += "<table><tr><th>"; h += t.thFlight; h += "</th><th>"; h += t.thRoute; h += "</th><th>";
    h += t.thType; h += "</th><th>"; h += t.thAlt; h += "</th><th>"; h += t.thKm; h += "</th></tr>";
    for (int i = 0; i < ws.count; i++) {
      const Flight &f = ws.list[i];
      h += i < ws.inRadius ? "<tr class=in>" : "<tr>";
      // FR24 data is untrusted: every field goes through esc()
      h += "<td>"; h += esc(*f.flight ? f.flight : f.callsign); h += "</td><td>";
      const char *a = airportCity(f.from), *b = airportCity(f.to);
      h += esc(a ? a : f.from); h += " → "; h += esc(b ? b : f.to);
      h += "</td><td>"; h += esc(f.type); h += "</td><td>";
      h += (int)(f.altFt * 0.3048f); h += " "; h += t.m; h += "</td><td>";
      h += String(f.distKm, 1); h += "</td></tr>";
    }
    h += "</table>";
  }
  h += "</div>";

  h += "<form method=post action=/save class=card><b>"; h += t.settings; h += "</b>";
  field(h, "lat", t.lat, String(cfg.lat, 6), "inputmode=decimal");
  field(h, "lon", t.lon, String(cfg.lon, 6), "inputmode=decimal");
  field(h, "radius", t.radius, String(cfg.radiusKm, 1), "type=number step=0.5 min=1 max=100");
  field(h, "area", t.area, String(cfg.areaKm, 0), "type=number min=1 max=200");
  field(h, "maxalt", t.maxAlt, String(cfg.maxAltM), "type=number min=0 max=15000");
  field(h, "bri", t.bri, String(cfg.brightness), "type=number min=1 max=255");
  field(h, "nbri", t.nightBri, String(cfg.nightBrightness), "type=number min=0 max=255");
  field(h, "nstart", t.nightFrom, String(cfg.nightStart), "type=number min=0 max=23");
  field(h, "nend", t.nightTo, String(cfg.nightEnd), "type=number min=0 max=23");
  field(h, "cycle", t.cycle, String(cfg.cycleSec), "type=number min=3 max=60");
  field(h, "poll", t.poll, String(cfg.pollSec), "type=number min=5 max=120");
  h += "<label><span>"; h += t.units; h += "</span><select name=metric>";
  option(h, "1", cfg.metric, t.metric);
  option(h, "0", !cfg.metric, t.imperial);
  h += "</select></label>";
  field(h, "view", t.view, String(cfg.viewHeading), "type=number min=0 max=359");
  h += "<label><span>"; h += t.orient; h += "</span><select name=flip>";
  option(h, "1", cfg.flip, t.usbLeft);
  option(h, "0", !cfg.flip, t.usbRight);
  h += "</select></label>";
  field(h, "tz", t.tz, esc(cfg.tz));
  h += "<p><button>"; h += t.save; h += "</button></p></form>";

  h += "<div class=card><form method=post action=/reboot style='display:inline'><button class=sec>";
  h += t.reboot; h += "</button></form> ";
  h += "<form method=post action=/wifireset style='display:inline' onsubmit=\"return confirm('";
  h += t.wifiConfirm; h += "')\"><button class=sec>"; h += t.wifiReset; h += "</button></form>";
  h += "<p><small>JSON: <a href=/api/flights>/api/flights</a> · "; h += t.ota;
  h += ": <code>pio run -e ota -t upload</code></small></p></div>";
  h += "</main></body></html>";
  server.send(200, "text/html; charset=utf-8", h);
}

static void handleSave() {
  auto f = [](const char *n) { return server.hasArg(n) ? server.arg(n) : String(); };
  if (f("lat").length()) cfg.lat = f("lat").toFloat();
  if (f("lon").length()) cfg.lon = f("lon").toFloat();
  if (f("radius").length()) cfg.radiusKm = f("radius").toFloat();
  if (f("area").length()) cfg.areaKm = f("area").toFloat();
  if (f("maxalt").length()) cfg.maxAltM = f("maxalt").toInt();
  if (f("bri").length()) cfg.brightness = f("bri").toInt();
  if (f("nbri").length()) cfg.nightBrightness = f("nbri").toInt();
  if (f("nstart").length()) cfg.nightStart = f("nstart").toInt();
  if (f("nend").length()) cfg.nightEnd = f("nend").toInt();
  if (f("cycle").length()) cfg.cycleSec = f("cycle").toInt();
  if (f("poll").length()) cfg.pollSec = f("poll").toInt();
  if (f("metric").length()) cfg.metric = f("metric").toInt() != 0;
  if (f("view").length()) cfg.viewHeading = f("view").toInt();
  if (f("flip").length()) cfg.flip = f("flip").toInt() != 0;
  if (f("tz").length()) strlcpy(cfg.tz, f("tz").c_str(), sizeof(cfg.tz));
  settingsSave();
  settingsLoad();  // re-applies the value limits
  displayApplyRotation();
  setenv("TZ", cfg.tz, 1);
  tzset();
  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleApi() {
  flightsGet(ws);
  JsonDocument d;
  d["ok"] = ws.ok;
  d["http"] = ws.lastHttp;
  d["age_s"] = ws.fetchedAt ? (millis() - ws.fetchedAt) / 1000 : -1;
  d["in_radius"] = ws.inRadius;
  JsonArray arr = d["flights"].to<JsonArray>();
  for (int i = 0; i < ws.count; i++) {
    const Flight &f = ws.list[i];
    JsonObject o = arr.add<JsonObject>();
    const char *an = airlineName(f.flight, f.airline, f.callsign);
    o["airline"] = an ? an : "";
    o["flight"] = f.flight; o["callsign"] = f.callsign; o["type"] = f.type; o["reg"] = f.reg;
    o["from"] = f.from; o["to"] = f.to; o["alt_ft"] = f.altFt; o["speed_kt"] = f.speedKt;
    o["track"] = f.track; o["vs_fpm"] = f.vspeedFpm; o["dist_km"] = f.distKm; o["bearing"] = f.bearing;
  }
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

void webuiBegin(void (*onWifiReset)()) {
  static void (*resetCb)() = nullptr;
  resetCb = onWifiReset;
  static const char *headers[] = {"Cookie", "Accept-Language"};
  server.collectHeaders(headers, 2);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/api/flights", HTTP_GET, handleApi);
  // lookup-table diagnostics: /api/lookup?flight=KC921&cs=KZR921&icao=KZR&ap=TVT&type=A21N
  server.on("/api/lookup", HTTP_GET, [] {
    JsonDocument d;
    String fl = server.arg("flight"), cs = server.arg("cs"), ic = server.arg("icao");
    String ap = server.arg("ap"), ty = server.arg("type");
    const char *al = airlineName(fl.c_str(), ic.c_str(), cs.c_str());
    const char *c = airportCity(ap.c_str());
    const char *t = aircraftName(ty.c_str());
    d["airline"] = al ? al : nullptr;
    d["city"] = c ? c : nullptr;
    d["aircraft"] = t ? t : nullptr;
    String out;
    serializeJson(d, out);
    server.send(200, "application/json", out);
  });
  server.on("/reboot", HTTP_POST, [] {
    server.send(200, "text/plain; charset=utf-8", lang().rebooting);
    delay(300);
    ESP.restart();
  });
  server.on("/wifireset", HTTP_POST, [] {
    server.send(200, "text/plain; charset=utf-8", lang().wifiDone);
    delay(300);
    if (resetCb) resetCb();
  });
  server.begin();
}

void webuiLoop() { server.handleClient(); }
