# OurAirports CSV Reference

## URLs (used by build script)

- Airports: `https://raw.githubusercontent.com/davidmegginson/ourairports-data/main/airports.csv`
- Runways: `https://raw.githubusercontent.com/davidmegginson/ourairports-data/main/runways.csv`

## Key airport columns

| Column | Use |
|--------|-----|
| `ident` | ICAO code (e.g. KJFK) |
| `type` | Filter: `large_airport` only |
| `latitude_deg`, `longitude_deg` | Airport center |
| `closed` | Skip if `"1"` |

## Key runway columns

| Column | Use |
|--------|-----|
| `airport_ident` | Join to airport ICAO |
| `le_ident`, `he_ident` | Runway ends (H* = helipad, excluded) |
| `le_latitude_deg`, `le_longitude_deg` | Low end coords |
| `he_latitude_deg`, `he_longitude_deg` | High end coords |
| `closed` | Skip if `"1"` |

## Coordinate format in firmware

Script converts decimal degrees to **e7 fixed-point** (`int32_t`): `round(deg * 1e7)`.
