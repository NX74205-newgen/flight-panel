#pragma once
#include <Arduino.h>

struct Flight {
  char id[12];        // FR24 internal id
  char flight[10];    // IATA flight number, e.g. "KC921"
  char callsign[10];  // ICAO callsign, e.g. "KZR921"
  char type[6];       // ICAO aircraft type, e.g. "A21N"
  char reg[12];       // registration
  char from[4];       // IATA code of the departure airport
  char to[4];         // IATA code of the destination airport
  char airline[4];    // airline ICAO code
  float lat, lon;
  int track;          // track, degrees
  int altFt;
  int speedKt;
  int vspeedFpm;
  bool onGround;
  uint32_t ts;        // unix time of the last position
  float distKm;       // distance from the observation point
  float bearing;      // bearing from the observation point to the aircraft
};

constexpr int MAX_FLIGHTS = 24;

struct FlightSnapshot {
  Flight list[MAX_FLIGHTS];  // sorted by distance
  int count = 0;             // total within the polling area
  int inRadius = 0;          // of those, within the display radius (the first inRadius entries)
  uint32_t fetchedAt = 0;    // millis() of the last successful poll
  bool ok = false;           // whether the last poll succeeded
  int lastHttp = 0;          // HTTP status of the last response
};

void flightsInit();

// Poll FR24 and update the snapshot. Returns true on success.
bool flightsFetch();

// Thread-safe copy of the current snapshot.
void flightsGet(FlightSnapshot &out);

float haversineKm(float lat1, float lon1, float lat2, float lon2);
float bearingDeg(float lat1, float lon1, float lat2, float lon2);
