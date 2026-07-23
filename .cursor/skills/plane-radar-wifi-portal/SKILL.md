---
name: plane-radar-wifi-portal
description: Configure WiFiManager captive portal, LAN config portal, mDNS, portal custom fields, and credential reset for Plane Radar. Use when adding portal settings, changing WiFi flow, or debugging setup/reconnect.
---

# Plane Radar WiFi Portal

## Portal modes

| Mode | When | Access |
|------|------|--------|
| Setup AP | No saved WiFi, or after credential wipe | SSID `PlaneRadar-Setup`, IP `192.168.4.1` |
| LAN portal | Connected to home WiFi | `http://plane-radar.local` or device IP |

Both use the same WiFiManager instance and custom fields. mDNS requires `-DWM_MDNS` in `platformio.ini`.

## Custom portal fields

Defined in `wifi_setup.cpp`:

| Parameter ID | Label | Saved by |
|--------------|-------|----------|
| `radar_lat` | Latitude (deg) | `services::location::saveFromStrings()` |
| `radar_lon` | Longitude (deg) | `services::location::saveFromStrings()` |
| `use_miles` | Display distances in miles | `ui::radar::saveMilesFromPortal()` |
| `show_runways` | Show airport runways | `ui::radar::saveRunwaysFromPortal()` |

Save callback: `onPortalParamsSaved()` via `wm.setSaveParamsCallback()`.

## NVS for portal state

- Namespace `wifi`, key `portal` — forces setup screen on next boot after credential wipe
- Separate from `radar` (lat/lon) and `planeradar` (range/units/runways)

## Boot flow

1. `wifiShowsSetupScreenOnBoot()` → yellow setup screen if portal forced
2. `wifiSetupConnect()` — connect with saved creds; open captive portal only on failure
3. `wifiLoop()` — keep LAN portal alive; call every `loop()` iteration
4. `wifiReconnect()` — background reconnect; never opens captive portal

## Credential reset

| Trigger | Action |
|---------|--------|
| BOOT hold 3 s | `wifiResetCredentialsAndReboot()` |
| BOOT at power-on (documented in README) | Same wipe path |

Wipe clears: WiFi creds, location (`services::location::clear()`), miles/runway prefs (`ui::radar::unitsReset()`). Range preset index is **not** reset.

## WiFi TX power

`WiFi.setTxPower(WIFI_POWER_8_5dBm)` in both AP start and STA connect paths. Do not increase without testing.

## Adding a new portal field

1. Add `WiFiManagerParameter` in `wifi_setup.cpp`
2. Register in `attachPortalParams()` and `refreshPortalParamDefaults()`
3. Persist in `onPortalParamsSaved()` — pick correct NVS namespace
4. Include in credential wipe if it should reset with BOOT long-press
5. Update README WiFi portal table

## Config constants

Portal names and timing in `include/config.h`:

- `kPortalApName`, `kPortalIp`, `kPortalHostname` (`plane-radar`)
- `kWifiConnectAttemptMs`, `kWifiConnectAttempts`
- `kWifiDownGraceMs`, `kWifiReconnectIntervalMs`
