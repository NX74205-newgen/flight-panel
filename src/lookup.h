#pragma once

// All functions return nullptr when the code is unknown.
const char *airportCity(const char *iata);
// flightNo: IATA flight number (KC921), icao: FR24 airline field, callsign: ICAO callsign (KZR921)
const char *airlineName(const char *flightNo, const char *icao, const char *callsign);
const char *aircraftName(const char *icaoType);
