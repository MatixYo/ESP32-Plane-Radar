---
name: plane-radar-airport-data
description: Regenerate the embedded major-airport runway dataset from OurAirports CSV data. Use when updating runways, airports, OurAirports data, or editing large_airports files.
---

# Plane Radar Airport Data

## Overview

Runway overlay data is **generated**, not hand-maintained. Source: [OurAirports](https://ourairports.com/data/) CSV exports.

## Regenerate workflow

```
- [ ] Run build script (needs network)
- [ ] Verify generated files changed
- [ ] Rebuild firmware
- [ ] Spot-check runway overlay near a known airport
```

```bash
python3 scripts/build_large_airports.py
pio run -e supermini
```

## Output files

| File | Role |
|------|------|
| `include/core/large_airports.h` | Struct definitions, lookup API |
| `src/core/large_airports_data.cpp` | Embedded runway coordinates (large generated blob) |

**Never hand-edit** `large_airports_data.cpp`. Change the Python script or CSV filters instead.

## Data rules (from script)

- Airports: `type == large_airport` only
- Runways: open (`closed != "1"`), helipad designators excluded (`H*` pattern)
- Coordinates stored as fixed-point e7 integers
- Runtime drawing: `ui::runway::drawLargeAirportRunways()` in `runway_overlay.cpp`

## Toggle at runtime

Users enable/disable overlay via WiFi portal checkbox **"Show airport runways"** (`ui::radar::showRunways()`).

## Adding filters or fields

1. Edit `scripts/build_large_airports.py`
2. Update `include/core/large_airports.h` struct if schema changes
3. Regenerate and update `runway_overlay.cpp` if draw logic needs new fields

For CSV column details, see [reference.md](reference.md).
