---
name: plane-radar-adsb
description: Work with the adsb.fi ADS-B client, aircraft parsing, fetch intervals, and radar refresh integration. Use when changing aircraft data, API calls, polling, or ground-aircraft filtering.
---

# Plane Radar ADS-B Client

## API

- Base: `https://opendata.adsb.fi/api/v3/lat/{lat}/lon/{lon}/dist/{nm}`
- Radius passed in **nautical miles** (converted from km in `fetchUpdate`)
- Public rate limit: ~1 req/s — respect `config::kAdsbFetchIntervalMs` (currently 3 s)

## Key files

| File | Role |
|------|------|
| `include/core/adsb.h` | `Aircraft` struct, `kMaxAircraft = 64` |
| `src/core/adsb.cpp` | HTTP fetch, JSON parse, aircraft buffer |
| `src/main.cpp` | Poll interval, calls `fetchAndDrawAircraft()` |
| `include/ui/radar_range.h` | `fetchRadiusKm()` for query radius |

## Aircraft struct

```cpp
struct Aircraft {
  float lat, lon;
  float nose_deg, track_deg, gs_knots;
  char callsign[9], type[5], alt[12];
};
```

## JSON field mapping

| Field | Maps to |
|-------|---------|
| `flight` (trimmed) → `callsign`; fallback `hex` | Callsign tag |
| `t` | Aircraft type tag |
| `alt_baro` / `alt_geom` | Altitude tag (`"ground"` → `"GND"`) |
| `true_heading` / `mag_heading` / `track` / `dir` | Nose heading (priority order) |
| `track` / headings | Speed vector direction |
| `gs` / `tas` / `ias` | Ground speed (knots) |

Ground aircraft filtered when `config::kAdsbShowGroundAircraft == false` (default).

## Non-blocking HTTP

Long requests must not freeze WiFi portal or BOOT button:

```cpp
services::adsb::setPollFn(wifiLoop);  // set once in setup()
```

`performGetWithPoll` and `readResponseBodyWithPoll` call the poll fn during I/O.

## Display integration

After successful fetch:

```cpp
services::adsb::fetchUpdate(lat, lon, ui::radar::fetchRadiusKm());
ui::radarDisplayRefreshAircraft();  // not radarDisplayDraw()
```

## Changing behavior checklist

- [ ] Fetch radius still uses `fetchRadiusKm()`, not ring label directly
- [ ] Poll fn still invoked during HTTP
- [ ] `kMaxAircraft` buffer not exceeded without truncation handling
- [ ] README ADS-B section updated if user-visible behavior changes
