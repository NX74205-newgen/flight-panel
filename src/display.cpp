// LED panel rendering: flight screen, clock screen, service screens. Runs in its own FreeRTOS task.
#include "display.h"
#include "flights.h"
#include "lookup.h"
#include "settings.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "font_panel.h"
#include <time.h>
#include <math.h>

// ---------- hardware: MatrixPortal S3 -> HUB75 ----------
static constexpr int W = 64, H = 32;
static MatrixPanel_I2S_DMA *mx = nullptr;

// ---------- palette ----------
static uint16_t C(uint8_t r, uint8_t g, uint8_t b) { return MatrixPanel_I2S_DMA::color565(r, g, b); }
static uint16_t COL_FLIGHT, COL_ROUTE, COL_ARROW, COL_DIM, COL_CITY, COL_AIRLINE, COL_PLANE,
    COL_REG, COL_SPEED, COL_UP, COL_DOWN, COL_LEVEL, COL_DIST, COL_CLOCK, COL_DATE, COL_ERR;

// ---------- UI state ----------
static volatile UiMode mode = UiMode::Boot;
static char modeMsg[48] = "";
static volatile bool nextRequested = false;
static char flashMsg[24] = "";
static uint32_t flashUntil = 0;

static FlightSnapshot snap;  // local copy used for drawing
static char curId[12] = "";  // flight currently on screen ("" = clock)
static uint32_t curSince = 0;
static uint32_t transStart = 0;
static int marqueeX = 0;
static uint32_t marqueePauseUntil = 0;

// Marquee segments, drawn in the classic GFX font (6 px per character). Segments are separated
// by " · " (18 px), and a 24 px gap is left before the text repeats.
static constexpr int CHAR_W = 6, SEP_W = 18, REPEAT_GAP = 24;
struct Seg { char text[40]; uint16_t color; };
static Seg segs[8];
static int nSegs = 0, segsWidth = 0;

// ---------- text helpers ----------
static void fontClassic() { mx->setFont(nullptr); mx->setTextSize(1); }
static void fontTiny() { mx->setFont(&PanelFont); mx->setTextSize(1); }

static int tinyWidth(const char *s) {  // string width in pixels for the current (proportional) font
  int16_t x1, y1; uint16_t w, h;
  mx->getTextBounds(s, 0, 10, &x1, &y1, &w, &h);
  return w;
}

static void print(int x, int y, uint16_t col, const char *s) {
  mx->setTextColor(col);
  mx->setCursor(x, y);
  mx->print(s);
}

static void addSeg(const char *text, uint16_t col) {
  if (nSegs >= 8 || !text || !*text) return;
  strlcpy(segs[nSegs].text, text, sizeof(segs[nSegs].text));
  segs[nSegs].color = col;
  segsWidth += strlen(segs[nSegs].text) * CHAR_W;
  nSegs++;
}

static const char *compass(float deg) {
  static const char *d[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return d[(int)((deg + 22.5f) / 45.0f) & 7];
}

static bool timeValid() { return time(nullptr) > 1700000000; }

// Dead-reckon the aircraft position between polls (track + speed) so the distance keeps moving.
static void livePos(const Flight &f, float &lat, float &lon) {
  lat = f.lat; lon = f.lon;
  if (!timeValid() || f.ts == 0) return;
  float dt = (float)((int64_t)time(nullptr) - (int64_t)f.ts);
  if (dt <= 0 || dt > 60) return;
  float km = f.speedKt * 1.852f * dt / 3600.0f;
  float tr = f.track * M_PI / 180.0f;
  lat += km * cosf(tr) / 111.0f;
  lon += km * sinf(tr) / (111.0f * cosf(lat * M_PI / 180.0f));
}

// ---------- marquee (scrolling line) for a flight ----------
static void buildMarquee(const Flight &f) {
  nSegs = 0; segsWidth = 0;
  char buf[48];
  const char *a = airportCity(f.from), *b = airportCity(f.to);
  if (*f.from || *f.to) {
    snprintf(buf, sizeof(buf), "%s \x1A %s", a ? a : (*f.from ? f.from : "?"), b ? b : (*f.to ? f.to : "?"));
    addSeg(buf, COL_CITY);
  }
  if (const char *al = airlineName(f.flight, f.airline, f.callsign)) addSeg(al, COL_AIRLINE);
  else if (*f.callsign) addSeg(f.callsign, COL_AIRLINE);
  if (const char *ac = aircraftName(f.type)) addSeg(ac, COL_PLANE);
  else if (*f.type) addSeg(f.type, COL_PLANE);
  if (*f.reg) addSeg(f.reg, COL_REG);
  if (cfg.metric) snprintf(buf, sizeof(buf), "%d km/h", (int)lroundf(f.speedKt * 1.852f));
  else snprintf(buf, sizeof(buf), "%d kt", f.speedKt);
  addSeg(buf, COL_SPEED);
  if (nSegs) segsWidth += (nSegs - 1) * SEP_W + REPEAT_GAP;
  marqueeX = 0;
  marqueePauseUntil = millis() + 1500;
}

static void drawMarquee(int y) {
  int total = segsWidth;
  int contentW = total - REPEAT_GAP;
  if (contentW <= W) {  // fits: show it static and centred
    int x = (W - contentW) / 2;
    for (int i = 0; i < nSegs; i++) {
      print(x, y, segs[i].color, segs[i].text);
      x += strlen(segs[i].text) * CHAR_W;
      if (i < nSegs - 1) { print(x + CHAR_W, y, COL_DIM, "\xFA"); x += SEP_W; }
    }
    return;
  }
  for (int rep = 0; rep < 2; rep++) {  // two copies back to back for a seamless loop
    int x = marqueeX + rep * total;
    for (int i = 0; i < nSegs; i++) {
      int w = strlen(segs[i].text) * CHAR_W;
      if (x < W && x + w > 0) print(x, y, segs[i].color, segs[i].text);
      x += w;
      if (i < nSegs - 1) {
        if (x + 2 * CHAR_W > 0 && x < W) print(x + CHAR_W, y, COL_DIM, "\xFA");
        x += SEP_W;
      }
    }
  }
}

static void stepMarquee() {
  int contentW = segsWidth - REPEAT_GAP;
  if (contentW <= W || millis() < marqueePauseUntil) return;
  marqueeX--;
  if (marqueeX <= -segsWidth) marqueeX += segsWidth;
}

// ---------- icon: anti-aliased plane silhouette rotated to the track ----------
// Outline in units of the radius, nose up (y grows downward). Right half only; the left half is mirrored.
static const float PLANE_HALF[][2] = {
    {0, -1}, {0.13f, -0.86f}, {0.14f, -0.22f}, {1.0f, 0.20f}, {1.0f, 0.34f}, {0.14f, 0.14f},
    {0.12f, 0.60f}, {0.42f, 0.84f}, {0.42f, 0.96f}, {0.06f, 0.90f}, {0, 0.98f}};
static constexpr int PH = sizeof(PLANE_HALF) / sizeof(PLANE_HALF[0]);
static constexpr int PN = PH * 2 - 2;
static constexpr int ICON = 15;           // icon size, px
static constexpr float ICON_R = 7.0f;     // silhouette radius, px
static uint8_t iconCov[ICON][ICON];      // per-pixel coverage, 0..16
static int iconTrack = -1;

static void planeVertex(int i, float &x, float &y) {
  if (i < PH) { x = PLANE_HALF[i][0]; y = PLANE_HALF[i][1]; }
  else { int j = PN - i; x = -PLANE_HALF[j][0]; y = PLANE_HALF[j][1]; }
}

static bool insidePlane(float x, float y) {
  bool c = false;
  for (int i = 0, j = PN - 1; i < PN; j = i++) {
    float xi, yi, xj, yj;
    planeVertex(i, xi, yi);
    planeVertex(j, xj, yj);
    if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) c = !c;
  }
  return c;
}

// Rasterise with 4x4 samples per pixel; recomputed only when the angle changes.
static void buildIcon(int trackDeg) {
  float a = trackDeg * M_PI / 180.0f, ca = cosf(a), sa = sinf(a);
  for (int py = 0; py < ICON; py++)
    for (int px = 0; px < ICON; px++) {
      int cov = 0;
      for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++) {
          float dx = px + (sx + 0.5f) / 4 - ICON / 2.0f, dy = py + (sy + 0.5f) / 4 - ICON / 2.0f;
          cov += insidePlane((dx * ca + dy * sa) / ICON_R, (-dx * sa + dy * ca) / ICON_R);
        }
      iconCov[py][px] = cov;
    }
  iconTrack = trackDeg;
}

static void drawPlaneIcon(int x0, int y0, int trackDeg, uint8_t r, uint8_t g, uint8_t b) {
  trackDeg = ((trackDeg % 360) + 360) % 360;
  if (trackDeg != iconTrack) buildIcon(trackDeg);
  for (int py = 0; py < ICON; py++)
    for (int px = 0; px < ICON; px++) {
      int c = iconCov[py][px];
      if (c < 3) continue;  // skip the faintest edge pixels: on LEDs they just look like noise
      mx->drawPixel(x0 + px, y0 + py, C(r * c / 16, g * c / 16, b * c / 16));
    }
}

static void drawTrend(int x, int baseY, int vs) {  // 5x3 up/down triangle centred on a PanelFont line
  if (vs > 300) mx->fillTriangle(x + 2, baseY - 4, x, baseY - 2, x + 4, baseY - 2, COL_UP);
  else if (vs < -300) mx->fillTriangle(x, baseY - 4, x + 4, baseY - 4, x + 2, baseY - 2, COL_DOWN);
  else mx->drawFastHLine(x, baseY - 3, 5, COL_LEVEL);
}

// ---------- screens ----------
static void drawFlight(const Flight &f, int yo) {
  // 1. flight number
  fontClassic();
  const char *num = *f.flight ? f.flight : (*f.callsign ? f.callsign : (*f.reg ? f.reg : "------"));
  print(0, 0 + yo, COL_FLIGHT, num);

  // 2. route (the plane icon sits to the right of rows 1-2)
  fontClassic();
  if (*f.from || *f.to) {
    print(0, 8 + yo, COL_ROUTE, *f.from ? f.from : "???");
    print(18, 8 + yo, COL_ARROW, "\x1A");
    print(24, 8 + yo, COL_ROUTE, *f.to ? f.to : "???");
  } else {
    print(0, 8 + yo, COL_REG, *f.reg ? f.reg : "");
  }
  // track relative to the window: top of the icon = the direction you look out of the window
  drawPlaneIcon(W - ICON, 0 + yo, f.track - cfg.viewHeading, 90, 200, 255);

  // 3. marquee
  drawMarquee(16 + yo);

  // 4. altitude + climb/descent trend ... compass direction + distance
  fontTiny();
  char alt[12], dist[16];
  uint16_t altCol = f.vspeedFpm > 300 ? COL_UP : (f.vspeedFpm < -300 ? COL_DOWN : COL_LEVEL);
  if (cfg.metric) snprintf(alt, sizeof(alt), "%dm", (int)lroundf(f.altFt * 0.3048f / 10) * 10);
  else snprintf(alt, sizeof(alt), "%dft", f.altFt / 10 * 10);
  print(0, 31 + yo, altCol, alt);
  int ax = tinyWidth(alt) + 2;
  drawTrend(ax, 31 + yo, f.vspeedFpm);

  float lat, lon;
  livePos(f, lat, lon);
  float d = haversineKm(cfg.lat, cfg.lon, lat, lon);
  float brg = bearingDeg(cfg.lat, cfg.lon, lat, lon);
  if (!cfg.metric) d /= 1.609f;
  const char *u = cfg.metric ? "km" : "mi";
  if (d < 10) snprintf(dist, sizeof(dist), "%.1f%s", d, u);
  else snprintf(dist, sizeof(dist), "%d%s", (int)lroundf(d), u);
  int dw = tinyWidth(dist);
  print(W - dw, 31 + yo, COL_DIST, dist);
  const char *cp = compass(brg);
  int cw = tinyWidth(cp);
  if (ax + 8 < W - dw - cw - 3) print(W - dw - cw - 3, 31 + yo, COL_DIM, cp);
}

static void drawClock(int yo) {
  fontClassic();
  mx->setTextSize(2);
  char hm[8] = "--:--";
  struct tm t;
  bool tv = timeValid();
  if (tv) {
    time_t now = time(nullptr);
    localtime_r(&now, &t);
    snprintf(hm, sizeof(hm), "%02d%c%02d", t.tm_hour, (millis() / 500) % 2 ? ':' : ' ', t.tm_min);
  }
  print(3, 0 + yo, COL_CLOCK, hm);
  mx->setTextSize(1);

  fontTiny();
  if (tv) {
    static const char *dn[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *mn[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    char date[20];
    snprintf(date, sizeof(date), "%s %d %s", dn[t.tm_wday], t.tm_mday, mn[t.tm_mon]);
    print((W - tinyWidth(date)) / 2, 22 + yo, COL_DATE, date);
  }

  char line[32];
  uint16_t col = COL_DIM;
  if (mode != UiMode::Run) {
    snprintf(line, sizeof(line), "%s", modeMsg);
  } else if (!snap.ok && snap.fetchedAt == 0) {
    snprintf(line, sizeof(line), snap.lastHttp ? "FR24 ERR %d" : "LOADING...", snap.lastHttp);
    col = snap.lastHttp ? COL_ERR : COL_DIM;
  } else if (snap.count > 0) {
    // nearest flight: number (amber) + distance (purple) + "+N" others (grey)
    const Flight &f = snap.list[0];
    float d = cfg.metric ? f.distKm : f.distKm / 1.609f;
    char num[12], dist[12], more[8] = "";
    snprintf(num, sizeof(num), "%s", *f.flight ? f.flight : f.callsign);
    snprintf(dist, sizeof(dist), "%d%s", (int)lroundf(d), cfg.metric ? "KM" : "MI");
    if (snap.count > 1) snprintf(more, sizeof(more), "+%d", snap.count - 1);
    const int gap = 3;  // space between words
    int wn = tinyWidth(num) + gap, wd = tinyWidth(dist), wm = *more ? gap + tinyWidth(more) : 0;
    if (wn + wd + wm > W) { more[0] = 0; wm = 0; }  // does not fit: drop the counter
    int x = (W - (wn + wd + wm)) / 2;
    print(x, 31 + yo, COL_FLIGHT, num);
    print(x + wn, 31 + yo, COL_DIST, dist);
    if (*more) print(x + wn + wd + gap, 31 + yo, COL_DIM, more);
  } else {
    snprintf(line, sizeof(line), "NO FLIGHTS");
  }
  if (mode != UiMode::Run || snap.count == 0 || (!snap.ok && snap.fetchedAt == 0))
    print((W - tinyWidth(line)) / 2, 31 + yo, col, line);
  // data is stale (>60 s): red dot in the corner
  if (mode == UiMode::Run && snap.fetchedAt && millis() - snap.fetchedAt > 60000) mx->drawPixel(W - 1, 0, COL_ERR);
}

static void drawLines(const char *l1, const char *l2, const char *l3, const char *l4) {
  fontTiny();
  const char *ls[] = {l1, l2, l3, l4};
  uint16_t cs[] = {COL_FLIGHT, COL_CITY, COL_ROUTE, COL_DIM};
  for (int i = 0; i < 4; i++)
    if (ls[i]) print((W - tinyWidth(ls[i])) / 2, 7 + i * 8, cs[i], ls[i]);
}

// ---------- brightness / night mode ----------
static int currentBrightness() {
  if (!timeValid()) return cfg.brightness;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int h = t.tm_hour;
  bool night = cfg.nightStart == cfg.nightEnd ? false
               : cfg.nightStart < cfg.nightEnd ? (h >= cfg.nightStart && h < cfg.nightEnd)
                                               : (h >= cfg.nightStart || h < cfg.nightEnd);
  return night ? cfg.nightBrightness : cfg.brightness;
}

// ---------- render loop ----------
static void renderTask(void *) {
  TickType_t last = xTaskGetTickCount();
  uint32_t lastSnap = 0;
  int lastBri = -1;
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(30));  // ~33 fps
    uint32_t now = millis();

    int bri = currentBrightness();
    if (bri != lastBri) { mx->setBrightness8(bri ? bri : 1); lastBri = bri; }
    mx->clearScreen();
    if (bri == 0) { mx->flipDMABuffer(); continue; }

    if (mode == UiMode::Boot) {
      drawLines("FLIGHT", "PANEL", nullptr, modeMsg);
    } else if (mode == UiMode::Portal) {
      drawLines("WIFI SETUP", "CONNECT TO", "FlightPanel", "-Setup");
    } else if (mode == UiMode::Connecting) {
      drawLines("CONNECTING", nullptr, modeMsg, nullptr);
    } else {
      if (now - lastSnap > 200) { flightsGet(snap); lastSnap = now; }

      // pick a flight: keep the current one while it is in range and its display time has not run out
      int idx = -1;
      for (int i = 0; i < snap.inRadius; i++)
        if (strcmp(snap.list[i].id, curId) == 0) { idx = i; break; }
      bool timeUp = now - curSince > (uint32_t)cfg.cycleSec * 1000 && snap.inRadius > 1;
      if (snap.inRadius > 0 && (idx < 0 || timeUp || nextRequested)) {
        int next = (idx < 0) ? 0 : (idx + 1) % snap.inRadius;
        idx = next;
        strlcpy(curId, snap.list[idx].id, sizeof(curId));
        curSince = now; transStart = now;
        buildMarquee(snap.list[idx]);
      } else if (snap.inRadius == 0 && curId[0]) {
        curId[0] = 0; transStart = now;
      }
      nextRequested = false;

      // new screen slides in from the bottom (300 ms, ease-out)
      int yo = 0;
      if (now - transStart < 300) {
        float p = (now - transStart) / 300.0f;
        yo = (int)lroundf(H * (1 - p) * (1 - p));
      }
      if (idx >= 0) { drawFlight(snap.list[idx], yo); if (yo == 0) stepMarquee(); }
      else drawClock(yo);
    }

    if ((int32_t)(flashUntil - now) > 0) {  // overlay message (brightness etc.); wrap-safe compare
      fontTiny();
      int w = tinyWidth(flashMsg);
      mx->fillRect((W - w) / 2 - 2, 10, w + 4, 11, 0);
      mx->drawRect((W - w) / 2 - 3, 9, w + 6, 13, COL_DIM);
      print((W - w) / 2, 18, COL_FLIGHT, flashMsg);
    }
    mx->flipDMABuffer();
  }
}

void displayInit() {
  HUB75_I2S_CFG::i2s_pins pins = {42, 41, 40, 38, 39, 37, 45, 36, 48, 35, 21, 47, 14, 2};
  HUB75_I2S_CFG mxcfg(W, H, 1, pins);
  mxcfg.double_buff = true;   // no flicker or tearing during animation
  mxcfg.clkphase = false;
  mx = new MatrixPanel_I2S_DMA(mxcfg);
  mx->begin();
  mx->setBrightness8(cfg.brightness);
  mx->setTextWrap(false);
  displayApplyRotation();
  mx->cp437(true);
  mx->clearScreen();
  mx->flipDMABuffer();

  COL_FLIGHT = C(255, 170, 0);
  COL_ROUTE = C(255, 255, 255);
  COL_ARROW = C(120, 120, 120);
  COL_DIM = C(130, 130, 150);
  COL_CITY = C(230, 230, 230);
  COL_AIRLINE = C(255, 210, 60);
  COL_PLANE = C(80, 200, 255);
  COL_REG = C(150, 150, 150);
  COL_SPEED = C(120, 255, 140);
  COL_UP = C(60, 230, 90);
  COL_DOWN = C(255, 110, 40);
  COL_LEVEL = C(220, 220, 220);
  COL_DIST = C(200, 140, 255);
  COL_CLOCK = C(70, 150, 255);
  COL_DATE = C(190, 190, 210);
  COL_ERR = C(255, 30, 30);
}

void displayStartTask() { xTaskCreatePinnedToCore(renderTask, "render", 6144, nullptr, 3, nullptr, 1); }

void displaySetMode(UiMode m, const char *msg) {
  strlcpy(modeMsg, msg ? msg : "", sizeof(modeMsg));
  mode = m;
}

void displayApplyRotation() { if (mx) mx->setRotation(cfg.flip ? 2 : 0); }

void displayNextFlight() { nextRequested = true; }

void displayFlash(const char *msg, uint32_t ms) {
  strlcpy(flashMsg, msg, sizeof(flashMsg));
  flashUntil = millis() + ms;
}
