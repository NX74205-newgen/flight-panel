# Flight Panel

Shows the flights passing your window on a 64×32 HUB75 LED panel (P5) driven by an
Adafruit MatrixPortal S3. Data comes from Flightradar24; everything runs on the ESP32-S3, no PC
or Raspberry Pi needed.

> [!WARNING]
> This project reads an **unofficial, undocumented** Flightradar24 endpoint. Flightradar24's
> [Terms and Conditions](https://www.flightradar24.com/terms-and-conditions) restrict automated
> access to their service and limit use of their data to personal, non-commercial purposes.
> Read the [disclaimer](#disclaimer-flightradar24-data) before using it.

## What it shows

A flight within the display radius (12 km by default):

```
KC921                       flight number          plane icon: track relative to your window
ALA→IST           ✈        route
Almaty → Istanbul · Air Astana · Airbus A321neo · P4-KDE · 760 km/h   (marquee)
3200m▲      NE 4.2km        altitude + climb/descent · where to look · distance
```

When nothing is close: a clock, the date and the nearest flight in the polling area (40 km).
When several flights are in range they rotate, nearest first.

## Hardware

- Adafruit MatrixPortal S3 (ESP32-S3, 8 MB flash, 2 MB PSRAM)
- HUB75 RGB LED panel 64×32, P5 (320×160 mm)
- 5 V power supply, at least 2–4 A (a USB-C PD power bank works too)

## First start

1. Power it up (USB-C).
2. On your phone, join the Wi-Fi network `FlightPanel-Setup`; the setup page opens
   (if it does not, open http://192.168.4.1). Pick a **2.4 GHz** network and enter the password.
3. Open the settings at http://flightpanel.local (or the IP shown by your router) and set:
   - **Latitude / longitude** of your window;
   - **Window faces, °** — the compass direction you look out of the window
     (the plane icon is drawn relative to it; 0 = north-up);
   - **Orientation** — USB port on the left or on the right.

The web UI is in English and Russian: the language follows your browser, and the RU/EN button
in the top-right corner switches it (the choice is remembered in a cookie).

## On-board buttons

- **UP** — brightness (5 steps)
- **DOWN** — next flight; hold for 5 s to forget Wi-Fi and start the setup access point

## Building

```
pip install platformio
pio run -e usb -t upload     # over USB (COM3 — change upload_port in platformio.ini)
pio run -e ota -t upload     # over Wi-Fi, once the panel is on the wall
```

OTA updates are not password-protected by default; see `OTA_PASSWORD` in `platformio.ini`.
The web UI has no authentication either — keep the panel on a trusted home network.

## HTTP API

- `GET /api/flights` — flights in the polling area as JSON
- `GET /api/lookup?flight=KC921&cs=KZR921&icao=KZR&ap=TVT&type=A21N` — check the lookup tables

## Project layout

| Path | Purpose |
|---|---|
| `src/main.cpp` | Wi-Fi setup, NTP, mDNS, OTA, buttons, polling loop |
| `src/flights.cpp` | FR24 feed client and parser, distance/bearing math |
| `src/display.cpp` | Rendering task: flight screen, clock, marquee, plane icon, night mode |
| `src/lookup.cpp` | Airport, airline and aircraft-type lookups |
| `src/webui.cpp` | Settings page and JSON API |
| `src/settings.*` | Settings stored in NVS |
| `tools/genfont.py` | Generates the 7 px `PanelFont` (`src/font_panel.h`) |
| `tools/genairports.py` | Generates `src/airports_db.h` |
| `tools/genairlines.py` | Generates `src/airlines_db.h` |

## Data sources

- **Flights:** the unofficial public Flightradar24 feed (`feed.js`), the same one their website
  uses. See the [disclaimer](#disclaimer-flightradar24-data) below.
  Over Almaty it is the only source with coverage (adsb.lol, airplanes.live and OpenSky showed nothing).
- **Cities:** a hand-made list in `src/lookup.cpp` first, then the full
  [OurAirports](https://ourairports.com/data/) database (~9,000 airports, public domain,
  `src/airports_db.h`; refresh with `python tools/genairports.py`).
- **Airlines:** a hand-made list in `src/lookup.cpp` (by flight number), then ICAO codes from the
  Wikipedia [List of airline codes](https://en.wikipedia.org/wiki/List_of_airline_codes)
  (~5,900 codes, CC BY-SA 4.0, `src/airlines_db.h`; refresh with `python tools/genairlines.py`).
  IATA codes are not taken from Wikipedia because they are widely reused by different airlines.
- **Aircraft types:** a hand-made list in `src/lookup.cpp`.

## Disclaimer: Flightradar24 data

- This project is **not affiliated with, endorsed by, or supported by Flightradar24 AB**.
  "Flightradar24" is a trademark of its owner and is used here only to describe the data source.
- The firmware polls `data-cloud.flightradar24.com/zones/fcgi/feed.js`, an **unofficial and
  undocumented** endpoint used by the Flightradar24 website. It is not a public API: it can
  change, be rate-limited, or block your IP address at any time without notice.
- Flightradar24's [Terms and Conditions](https://www.flightradar24.com/terms-and-conditions)
  restrict automated access to their service and allow use of their data for personal,
  non-commercial purposes only. Using this firmware may not comply with those terms.
- **You alone are responsible** for how you use this software and for complying with
  Flightradar24's terms and any applicable law. The authors accept no liability for any
  consequences (see also the warranty disclaimer in [LICENSE](LICENSE)).
- Use it **only for personal, non-commercial purposes**: do not sell devices running this
  firmware, do not redistribute or republish the flight data it receives, and do not lower the
  polling interval below the default (8 s) or run many panels against the same endpoint.
- No Flightradar24 data is stored in or distributed with this repository.
- **Compliant alternatives:**
  - the official [Flightradar24 API](https://fr24api.flightradar24.com/) (paid subscription);
    only `src/flights.cpp` needs to be adapted;
  - your own ADS-B receiver (RTL-SDR dongle + antenna, e.g. readsb/dump1090 on a Raspberry Pi),
    whose local JSON can be read over your home network;
  - community aggregators with open data policies such as [adsb.lol](https://adsb.lol) or
    [airplanes.live](https://airplanes.live), where they have coverage.

If you are a rights holder and have concerns about this project, please open an issue.

## License

[MIT](LICENSE): you may use this code in any project, including commercial ones, as long as you
keep the copyright notice with the link to this repository
(https://github.com/NX74205-newgen/flight-panel).
`src/airlines_db.h` is derived from Wikipedia and stays under CC BY-SA 4.0; see [NOTICE](NOTICE).
